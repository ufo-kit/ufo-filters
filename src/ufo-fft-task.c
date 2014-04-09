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

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#ifdef HAVE_FFTW3
#include <fftw3.h>
#endif

#include "clFFT.h"
#include "ufo-fft-task.h"


struct _UfoFftTaskPrivate {
    enum {
        FFT_1D = 1,
        FFT_2D,
        FFT_3D
    } fft_dimensions;

    cl_context  context;
    cl_kernel   kernel;
    clFFT_Plan  fft_plan;
    clFFT_Dim3  fft_size;

    gboolean auto_zeropadding;
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

    if (priv->auto_zeropadding) {
        priv->kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_spread", error);
    }

    priv->context = ufo_resources_get_context (resources);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
}

static void
ufo_fft_task_get_requisition (UfoTask *task,
                              UfoBuffer **inputs,
                              UfoRequisition *requisition)
{
    UfoFftTaskPrivate *priv;
    UfoRequisition in_req;
    clFFT_Dimension dimension;
    cl_int cl_err;

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &in_req);

    priv->fft_size.x = (priv->auto_zeropadding) ? pow2round ((guint32) in_req.dims[0]) : 
                                                  (guint) in_req.dims[0]/2;

    switch (priv->fft_dimensions) {
        case FFT_1D:
            dimension = clFFT_1D;
            break;
        case FFT_2D:
            priv->fft_size.y = (priv->auto_zeropadding) ? pow2round ((guint32) in_req.dims[1]) :
                                                          (guint) in_req.dims[1];
            dimension = clFFT_2D;
            break;
        case FFT_3D:
            dimension = clFFT_3D;
            break;
    }

    if (priv->fft_plan == NULL) {
        priv->fft_plan = clFFT_CreatePlan (priv->context,
                                           priv->fft_size,
                                           dimension,
                                           clFFT_InterleavedComplexFormat, 
                                           &cl_err);
        UFO_RESOURCES_CHECK_CLERR (cl_err);
    }

    requisition->n_dims = 2;
    requisition->dims[0] = 2 * priv->fft_size.x;
    requisition->dims[1] = priv->fft_dimensions == FFT_1D ? in_req.dims[1] : priv->fft_size.y;
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
    return UFO_FFT_TASK_GET_PRIVATE (task)->fft_dimensions > 2 ? 3 : 2;
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
    UfoGpuNode *node;
    UfoRequisition in_req;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_event event;
    cl_int width;
    cl_int height;
    gsize global_work_size[2];

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    ufo_buffer_get_requisition (inputs[0], &in_req);

    if (priv->auto_zeropadding){
        width = (cl_int) in_req.dims[0];
        height = (cl_int) in_req.dims[1];

        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), (gpointer) &out_mem));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), (gpointer) &in_mem));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_int), &width));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (cl_int), &height));

        global_work_size[0] = requisition->dims[0] >> 1;
        global_work_size[1] = requisition->dims[1];

        UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue,
                                                           priv->kernel,
                                                           2, NULL, global_work_size, NULL,
                                                           0, NULL, &event));
    }
    
    if (priv->fft_dimensions == FFT_1D) {
        clFFT_ExecuteInterleaved_Ufo (cmd_queue, priv->fft_plan,
				      (cl_int) in_req.dims[1], clFFT_Forward,
				      (priv->auto_zeropadding)? out_mem : in_mem, out_mem,
				      (priv->auto_zeropadding)? 1 : 0, 
				      (priv->auto_zeropadding)? &event : NULL, NULL, profiler);
    }
    else {
        clFFT_ExecuteInterleaved_Ufo (cmd_queue, priv->fft_plan,
				      1, clFFT_Forward,
				      (priv->auto_zeropadding)? out_mem : in_mem, out_mem, 
				      (priv->auto_zeropadding)? 1 : 0, 
				      (priv->auto_zeropadding)? &event : NULL, NULL, profiler);
    }

    if (priv->auto_zeropadding) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseEvent (event));
    }
    

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

    clFFT_DestroyPlan (priv->fft_plan);

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
            priv->auto_zeropadding = g_value_get_boolean (value);
            break;
        case PROP_DIMENSIONS:
            priv->fft_dimensions = g_value_get_uint (value);
            break;
        case PROP_SIZE_X:
            priv->fft_size.x = g_value_get_uint (value);
            break;
        case PROP_SIZE_Y:
            priv->fft_size.y = g_value_get_uint (value);
            break;
        case PROP_SIZE_Z:
            priv->fft_size.z = g_value_get_uint (value);
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
            g_value_set_boolean (value, priv->auto_zeropadding);
            break;
        case PROP_DIMENSIONS:
            g_value_set_uint (value, priv->fft_dimensions);
            break;
        case PROP_SIZE_X:
            g_value_set_uint (value, priv->fft_size.x);
            break;
        case PROP_SIZE_Y:
            g_value_set_uint (value, priv->fft_size.y);
            break;
        case PROP_SIZE_Z:
            g_value_set_uint (value, priv->fft_size.z);
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
            1, 8192, 1,
            G_PARAM_READWRITE);

    properties[PROP_SIZE_Y] =
        g_param_spec_uint("size-y",
            "Size of the FFT transform in y-direction",
            "Size of the FFT transform in y-direction",
            1, 8192, 1,
            G_PARAM_READWRITE);

    properties[PROP_SIZE_Z] =
        g_param_spec_uint("size-z",
            "Size of the FFT transform in z-direction",
            "Size of the FFT transform in z-direction",
            1, 8192, 1,
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
    priv->fft_dimensions = FFT_1D;
    priv->fft_size.x = 1;
    priv->fft_size.y = 1;
    priv->fft_size.z = 1;
    priv->fft_plan = NULL;
    priv->kernel = NULL;

    priv->auto_zeropadding = TRUE;
}
