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

#ifdef HAVE_OCLFFT
#include <clFFT.h>
#endif

#ifdef HAVE_FFTW3
#include <fftw3.h>
#endif

#include <ufo-gpu-task-iface.h>
#include "ufo-fft-task.h"

/**
 * SECTION:ufo-fft-task
 * @Short_description: Compute fast discrete Fourier transform
 * @Title: fft
 */

struct _UfoFftTaskPrivate {
    enum {
        FFT_1D = 1,
        FFT_2D,
        FFT_3D
    } fft_dimensions;

#ifdef HAVE_OCLFFT
    cl_context  context;
    cl_kernel   kernel;
    clFFT_Plan  fft_plan;
    clFFT_Dim3  fft_size;
#endif
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFftTask, ufo_fft_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init)
                         )

#define UFO_FFT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FFT_TASK, UfoFftTaskPrivate))

enum {
    PROP_0,
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
#ifdef HAVE_OCLFFT
    UfoFftTaskPrivate *priv;

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    priv->kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_spread", error);
    priv->context = ufo_resources_get_context (resources);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
#endif
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

    priv->fft_size.x = pow2round ((guint32) in_req.dims[0]);


    switch (priv->fft_dimensions) {
        case FFT_1D:
            dimension = clFFT_1D;
            break;
        case FFT_2D:
            priv->fft_size.y = pow2round ((guint32) in_req.dims[1]);
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

static void
ufo_fft_task_get_structure (UfoTask *task,
                            guint *n_inputs,
                            UfoInputParam **in_params,
                            UfoTaskMode *mode)
{
    UfoFftTaskPrivate *priv;

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = priv->fft_dimensions;
    (*in_params)[0].n_expected = -1;
}

static UfoNode *
ufo_fft_task_copy_real (UfoNode *node,
                        GError **error)
{
    UfoFftTask *orig;
    UfoFftTask *copy;

    orig = UFO_FFT_TASK (node);
    copy = UFO_FFT_TASK (ufo_fft_task_new ());

    g_object_set (G_OBJECT (copy),
                  "dimensions", orig->priv->fft_dimensions,
                  "size-x", orig->priv->fft_size.x,
                  "size-y", orig->priv->fft_size.y,
                  "size-z", orig->priv->fft_size.z,
                  NULL);

    return UFO_NODE (copy);
}

static gboolean
ufo_fft_task_equal_real (UfoNode *n1,
                            UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_FFT_TASK (n1) && UFO_IS_FFT_TASK (n2), FALSE);
    return UFO_FFT_TASK (n1)->priv->kernel == UFO_FFT_TASK (n2)->priv->kernel;
}

static void
ufo_fft_task_finalize (GObject *object)
{
    UfoFftTaskPrivate *priv;

    priv = UFO_FFT_TASK_GET_PRIVATE (object);

#ifdef HAVE_OCLFFT
    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    clFFT_DestroyPlan (priv->fft_plan);
#endif

    G_OBJECT_CLASS (ufo_fft_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_fft_task_setup;
    iface->get_requisition = ufo_fft_task_get_requisition;
    iface->get_structure = ufo_fft_task_get_structure;
}

#ifdef HAVE_OCLFFT
static gboolean
ufo_fft_task_process_gpu (UfoGpuTask *task,
                          UfoBuffer **inputs,
                          UfoBuffer *output,
                          UfoRequisition *requisition,
                          UfoGpuNode *node)
{
    UfoFftTaskPrivate *priv;
    UfoRequisition in_req;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_event event;
    cl_int width;
    cl_int height;
    gsize global_work_size[2];

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    ufo_buffer_get_requisition (inputs[0], &in_req);
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

    if (priv->fft_dimensions == FFT_1D) {
        clFFT_ExecuteInterleaved (cmd_queue, priv->fft_plan,
                                  (cl_int) in_req.dims[1], clFFT_Forward,
                                  out_mem, out_mem,
                                  1, &event, NULL);
    }
    else {
        clFFT_ExecuteInterleaved (cmd_queue, priv->fft_plan,
                                  1, clFFT_Forward,
                                  out_mem, out_mem, 
                                  1, &event, NULL);
    }

    UFO_RESOURCES_CHECK_CLERR (clReleaseEvent (event));

    return TRUE;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_fft_task_process_gpu;
}
#endif

static void
ufo_fft_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoFftTaskPrivate *priv = UFO_FFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
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

    node_class->copy = ufo_fft_task_copy_real;
    node_class->equal = ufo_fft_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoFftTaskPrivate));
}

static void
ufo_fft_task_init (UfoFftTask *self)
{
    UfoFftTaskPrivate *priv;
    self->priv = priv = UFO_FFT_TASK_GET_PRIVATE (self);
#ifdef HAVE_OCLFFT
    priv->fft_dimensions = FFT_1D;
    priv->fft_size.x = 1;
    priv->fft_size.y = 1;
    priv->fft_size.z = 1;
    priv->fft_plan = NULL;
    priv->kernel = NULL;
#endif
}
