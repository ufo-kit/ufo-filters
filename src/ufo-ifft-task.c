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

#include "ufo-ifft-task.h"

/**
 * SECTION:ufo-ifft-task
 * @Short_description: Compute inverse Fast Fourier transform
 * @Title: ifft
 */

struct _UfoIfftTaskPrivate {
    enum {
        FFT_1D = 1,
        FFT_2D,
        FFT_3D
    } fft_dimensions;

#ifdef HAVE_OCLFFT
    cl_context  context;
    cl_kernel   kernel;
    clFFT_Plan  fft_plan;
#endif

    gint crop_width;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoIfftTask, ufo_ifft_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_IFFT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_IFFT_TASK, UfoIfftTaskPrivate))

enum {
    PROP_0,
    PROP_DIMENSIONS,
    PROP_CROP_WIDTH,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_ifft_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_IFFT_TASK, NULL));
}

static void
ufo_ifft_task_setup (UfoTask *task,
                     UfoResources *resources,
                     GError **error)
{
#ifdef HAVE_OCLFFT
    UfoIfftTaskPrivate *priv;

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    priv->kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_pack", error);
    priv->context = ufo_resources_get_context (resources);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
#endif
}

static void
ufo_ifft_task_get_requisition (UfoTask *task,
                               UfoBuffer **inputs,
                               UfoRequisition *requisition)
{
    UfoIfftTaskPrivate *priv;
    UfoRequisition in_req;
    clFFT_Dim3 fft_size;
    clFFT_Dimension dimension;
    cl_int cl_err = CL_SUCCESS;

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &in_req);

    fft_size.x = (guint32) in_req.dims[0] / 2;
    fft_size.y = 1;
    fft_size.z = 1;

    switch (priv->fft_dimensions) {
        case FFT_1D:
            dimension = clFFT_1D;
            break;
        case FFT_2D:
            dimension = clFFT_2D;
            fft_size.y = (guint32) in_req.dims[1];
            break;
        case FFT_3D:
            dimension = clFFT_3D;
            break;
    }

    if (priv->fft_plan == NULL) {
        priv->fft_plan = clFFT_CreatePlan (priv->context,
                                           fft_size, dimension,
                                           clFFT_InterleavedComplexFormat,
                                           &cl_err);
        UFO_RESOURCES_CHECK_CLERR (cl_err);
    }

    requisition->n_dims = 2;
    requisition->dims[0] = priv->crop_width > 0 ? (gsize) priv->crop_width : fft_size.x;
    requisition->dims[1] = in_req.dims[1];
}

static void
ufo_ifft_task_get_structure (UfoTask *task,
                             guint *n_inputs,
                             UfoInputParam **in_params,
                             UfoTaskMode *mode)
{
    UfoIfftTaskPrivate *priv;

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = priv->fft_dimensions;
}

static UfoNode *
ufo_ifft_task_copy_real (UfoNode *node,
                        GError **error)
{
    UfoIfftTask *orig;
    UfoIfftTask *copy;

    orig = UFO_IFFT_TASK (node);
    copy = UFO_IFFT_TASK (ufo_ifft_task_new ());

    g_object_set (G_OBJECT (copy),
                  "dimensions", orig->priv->fft_dimensions,
                  "crop-width", orig->priv->crop_width,
                  NULL);

    return UFO_NODE (copy);
}

static gboolean
ufo_ifft_task_equal_real (UfoNode *n1,
                          UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_IFFT_TASK (n1) && UFO_IS_IFFT_TASK (n2), FALSE);
    return TRUE;
}

static void
ufo_ifft_task_finalize (GObject *object)
{
    UfoIfftTaskPrivate *priv;

    priv = UFO_IFFT_TASK_GET_PRIVATE (object);

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

    G_OBJECT_CLASS (ufo_ifft_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_ifft_task_setup;
    iface->get_requisition = ufo_ifft_task_get_requisition;
    iface->get_structure = ufo_ifft_task_get_structure;
}

#ifdef HAVE_OCLFFT
static gboolean
ufo_ifft_task_process_gpu (UfoGpuTask *task,
                          UfoBuffer **inputs,
                          UfoBuffer *output,
                          UfoRequisition *requisition,
                          UfoGpuNode *node)
{
    UfoIfftTaskPrivate *priv;
    UfoRequisition in_req;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_int batch_size;
    cl_int width;
    gfloat scale;
    gsize global_work_size[2];

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    batch_size = priv->fft_dimensions == FFT_1D ? (cl_int) requisition->dims[1] : 1;

    clFFT_ExecuteInterleaved (cmd_queue,
                              priv->fft_plan, batch_size, clFFT_Inverse,
                              in_mem, in_mem,
                              0, NULL, NULL);

    clFinish (cmd_queue);

    scale = 1.0f / ((gfloat) requisition->dims[0]);

    if (priv->fft_dimensions == FFT_2D)
        scale /= (gfloat) requisition->dims[0];

    width = priv->crop_width > 0 ? priv->crop_width : (cl_int) requisition->dims[0];
    ufo_buffer_get_requisition (inputs[0], &in_req);
    global_work_size[0] = in_req.dims[0] >> 1;
    global_work_size[1] = in_req.dims[1];

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), (gpointer) &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), (gpointer) &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_int), &width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (gfloat), &scale));

    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue,
                                                       priv->kernel,
                                                       2, NULL, global_work_size, NULL,
                                                       0, NULL, NULL));
    return TRUE;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_ifft_task_process_gpu;
}
#endif

static void
ufo_ifft_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoIfftTaskPrivate *priv = UFO_IFFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_DIMENSIONS:
            priv->fft_dimensions = g_value_get_uint (value);
            break;
        case PROP_CROP_WIDTH:
            priv->crop_width = g_value_get_int (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_ifft_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoIfftTaskPrivate *priv = UFO_IFFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_DIMENSIONS:
            g_value_set_uint (value, priv->fft_dimensions);
            break;
        case PROP_CROP_WIDTH:
            g_value_set_int (value, priv->crop_width);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_ifft_task_class_init (UfoIfftTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;
    
    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->finalize = ufo_ifft_task_finalize;
    oclass->set_property = ufo_ifft_task_set_property;
    oclass->get_property = ufo_ifft_task_get_property;

    properties[PROP_DIMENSIONS] =
        g_param_spec_uint ("dimensions",
                           "Number of IFFT dimensions from 1 to 3",
                           "Number of IFFT dimensions from 1 to 3",
                           1, 3, 1,
                           G_PARAM_READWRITE);

    properties[PROP_CROP_WIDTH] =
        g_param_spec_int ("crop-width",
                          "Width of cropped output",
                          "Width of cropped output",
                          -1, G_MAXINT, -1,
                          G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->copy = ufo_ifft_task_copy_real;
    node_class->equal = ufo_ifft_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoIfftTaskPrivate));
}

static void
ufo_ifft_task_init (UfoIfftTask *self)
{
    UfoIfftTaskPrivate *priv;
    self->priv = priv = UFO_IFFT_TASK_GET_PRIVATE (self);
    priv->crop_width = -1;
#ifdef HAVE_OCLFFT
    priv->fft_dimensions = FFT_1D;
    priv->fft_plan = NULL;
    priv->kernel = NULL;
    priv->context = NULL;
#endif
}
