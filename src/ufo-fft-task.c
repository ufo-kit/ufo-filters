/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"

#include <string.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-fft-task.h"
#include "common/ufo-fft.h"


struct _UfoFftTaskPrivate {
    UfoFft *fft;
    UfoFftParameter param;

    cl_context context;
    cl_kernel kernel;

    gboolean zeropad;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFftTask, ufo_fft_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_FFT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FFT_TASK, UfoFftTaskPrivate))

enum {
    PROP_0,
    PROP_ZEROPADDING,
    PROP_DIMENSIONS,
    PROP_SIZE_X,
    PROP_SIZE_Y,
    PROP_SIZE_Z,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_fft_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FFT_TASK, NULL));
}

static guint32
pow2round(guint32 x)
{
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x+1;
}

static void
ufo_fft_task_setup (UfoTask *task,
                    UfoResources *resources,
                    GError **error)
{
    UfoFftTaskPrivate *priv;

    priv = UFO_FFT_TASK_GET_PRIVATE (task);

    if (priv->zeropad) {
        priv->kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_spread", NULL, error);
    }

    priv->context = ufo_resources_get_context (resources);
    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainContext (priv->context), error);

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->kernel), error);
}

static void
ufo_fft_task_get_requisition (UfoTask *task,
                              UfoBuffer **inputs,
                              UfoRequisition *requisition,
                              GError **error)
{
    UfoFftTaskPrivate *priv;
    UfoRequisition in_req;
    cl_command_queue queue;

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &in_req);

    priv->param.zeropad = priv->zeropad;
    priv->param.size[0] = priv->zeropad ? pow2round (in_req.dims[0]) : in_req.dims[0] / 2;

    switch (priv->param.dimensions) {
        case UFO_FFT_1D:
            priv->param.batch = in_req.n_dims == 2 ? in_req.dims[1] : 1;
            break;

        case UFO_FFT_2D:
            priv->param.size[1] = priv->zeropad ? pow2round (in_req.dims[1]) : in_req.dims[1];
            priv->param.batch = in_req.n_dims == 3 ? in_req.dims[2] : 1;
            break;

        case UFO_FFT_3D:
            break;
    }

    queue = ufo_gpu_node_get_cmd_queue (UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task))));
    UFO_RESOURCES_CHECK_SET_AND_RETURN (ufo_fft_update (priv->fft, priv->context, queue, &priv->param), error);

    *requisition = in_req;  /* keep third dimension for 2D batching */
    requisition->dims[0] = 2 * priv->param.size[0];
    requisition->dims[1] = priv->param.dimensions == UFO_FFT_1D ? in_req.dims[1] : priv->param.size[1];
}

static guint
ufo_fft_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_fft_task_get_num_dimensions (UfoTask *task,
                                 guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return UFO_FFT_TASK_GET_PRIVATE (task)->param.dimensions > 2 ? 3 : 2;
}

static UfoTaskMode
ufo_fft_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_fft_task_equal_real (UfoNode *n1,
                            UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_FFT_TASK (n1) && UFO_IS_FFT_TASK (n2), FALSE);
    return UFO_FFT_TASK (n1)->priv->kernel == UFO_FFT_TASK (n2)->priv->kernel;
}

static gboolean
ufo_fft_task_process (UfoTask *task,
                      UfoBuffer **inputs,
                      UfoBuffer *output,
                      UfoRequisition *requisition)
{
    UfoFftTaskPrivate *priv;
    UfoRequisition in_req;
    UfoProfiler *profiler;
    cl_command_queue queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_int width;
    cl_int height;
    gsize global_work_size[3];

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    queue = ufo_gpu_node_get_cmd_queue (UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task))));
    in_mem = ufo_buffer_get_device_array (inputs[0], queue);
    out_mem = ufo_buffer_get_device_array (output, queue);

    ufo_buffer_get_requisition (inputs[0], &in_req);
    ufo_buffer_set_layout (output, UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED);

    if (priv->zeropad){
        width = (cl_int) in_req.dims[0];
        height = (cl_int) in_req.dims[1];

        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), (gpointer) &out_mem));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), (gpointer) &in_mem));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_int), &width));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (cl_int), &height));

        global_work_size[0] = requisition->dims[0] >> 1;
        global_work_size[1] = requisition->dims[1];
        global_work_size[2] = requisition->n_dims == 3 ? requisition->dims[2] : 1;

        UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (queue, priv->kernel,
                                                           3, NULL, global_work_size, NULL,
                                                           0, NULL, NULL));
    }

    UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (priv->fft, queue, profiler,
                                                priv->zeropad ? out_mem : in_mem,
                                                out_mem, UFO_FFT_FORWARD,
                                                0, NULL, NULL));

    return TRUE;
}

static void
ufo_fft_task_finalize (GObject *object)
{
    UfoFftTaskPrivate *priv;

    priv = UFO_FFT_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    if (priv->fft) {
        ufo_fft_destroy (priv->fft);
        priv->fft = NULL;
    }

    G_OBJECT_CLASS (ufo_fft_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_fft_task_setup;
    iface->get_requisition = ufo_fft_task_get_requisition;
    iface->get_num_inputs = ufo_fft_task_get_num_inputs;
    iface->get_num_dimensions = ufo_fft_task_get_num_dimensions;
    iface->get_mode = ufo_fft_task_get_mode;
    iface->process = ufo_fft_task_process;
}

static void
ufo_fft_task_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
    UfoFftTaskPrivate *priv = UFO_FFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_ZEROPADDING:
            priv->zeropad = g_value_get_boolean (value);
            break;
        case PROP_DIMENSIONS:
            priv->param.dimensions = g_value_get_uint (value);
            break;
        case PROP_SIZE_X:
            priv->param.size[0] = g_value_get_uint (value);
            break;
        case PROP_SIZE_Y:
            priv->param.size[1] = g_value_get_uint (value);
            break;
        case PROP_SIZE_Z:
            priv->param.size[2] = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_fft_task_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
    UfoFftTaskPrivate *priv = UFO_FFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_ZEROPADDING:
            g_value_set_boolean (value, priv->zeropad);
            break;
        case PROP_DIMENSIONS:
            g_value_set_uint (value, priv->param.dimensions);
            break;
        case PROP_SIZE_X:
            g_value_set_uint (value, priv->param.size[0]);
            break;
        case PROP_SIZE_Y:
            g_value_set_uint (value, priv->param.size[1]);
            break;
        case PROP_SIZE_Z:
            g_value_set_uint (value, priv->param.size[2]);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_fft_task_class_init (UfoFftTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;

    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->finalize = ufo_fft_task_finalize;
    oclass->set_property = ufo_fft_task_set_property;
    oclass->get_property = ufo_fft_task_get_property;

    properties[PROP_ZEROPADDING] =
        g_param_spec_boolean("auto-zeropadding",
            "Auto zeropadding to next power of 2 value",
            "Auto zeropadding to next power of 2 value",
            TRUE,
            G_PARAM_READWRITE);

    properties[PROP_DIMENSIONS] =
        g_param_spec_uint("dimensions",
            "Number of FFT dimensions from 1 to 3",
            "Number of FFT dimensions from 1 to 3",
            1, 3, 1,
            G_PARAM_READWRITE);

    properties[PROP_SIZE_X] =
        g_param_spec_uint("size-x",
            "Size of the FFT transform in x-direction",
            "Size of the FFT transform in x-direction",
            1, 32768, 1,
            G_PARAM_READWRITE);

    properties[PROP_SIZE_Y] =
        g_param_spec_uint("size-y",
            "Size of the FFT transform in y-direction",
            "Size of the FFT transform in y-direction",
            1, 32768, 1,
            G_PARAM_READWRITE);

    properties[PROP_SIZE_Z] =
        g_param_spec_uint("size-z",
            "Size of the FFT transform in z-direction",
            "Size of the FFT transform in z-direction",
            1, 32768, 1,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->equal = ufo_fft_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoFftTaskPrivate));
}

static void
ufo_fft_task_init (UfoFftTask *self)
{
    UfoFftTaskPrivate *priv;

    self->priv = priv = UFO_FFT_TASK_GET_PRIVATE (self);

    priv->kernel = NULL;
    priv->zeropad = TRUE;
    priv->fft = ufo_fft_new ();
    priv->param.dimensions = UFO_FFT_1D;
    priv->param.size[0] = 1;
    priv->param.size[1] = 1;
    priv->param.size[2] = 1;
    priv->param.batch = 1;
    priv->param.zeropad = priv->zeropad = TRUE;
}
