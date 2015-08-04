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

#ifdef HAVE_AMD
#include <clFFT.h>
#else
#include "oclFFT.h"
#endif

#include "ufo-ifft-task.h"

struct _UfoIfftTaskPrivate {
    enum {
        FFT_1D = 1,
        FFT_2D,
        FFT_3D
    } fft_dimensions;

    #ifdef HAVE_AMD
    clfftPlanHandle fft_plan;
    clfftSetupData fft_setup;
    size_t fft_size[3];
    #else
    clFFT_Plan fft_plan;
    clFFT_Dim3 fft_size;
    #endif

    cl_context  context;
    cl_kernel   kernel;
    cl_command_queue cmd_queue;

    cl_int batch_size;
    gint crop_width;
    gint crop_height;
};

#ifdef HAVE_AMD
#define clFFT_1D CLFFT_1D
#define clFFT_2D CLFFT_2D
#define clFFT_3D CLFFT_3D
#endif

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoIfftTask, ufo_ifft_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_IFFT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_IFFT_TASK, UfoIfftTaskPrivate))

enum {
    PROP_0,
    PROP_DIMENSIONS,
    PROP_CROP_WIDTH,
    PROP_CROP_HEIGHT,
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
    UfoIfftTaskPrivate *priv;

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    priv->kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_pack", error);
    priv->context = ufo_resources_get_context (resources);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    if (priv->kernel != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
    }
}

static void
ufo_ifft_task_get_requisition (UfoTask *task,
                               UfoBuffer **inputs,
                               UfoRequisition *requisition)
{
    UfoGpuNode *node;
    UfoIfftTaskPrivate *priv;
    UfoRequisition in_req;
    guint32 x_dim = 1;
    guint32 y_dim = 1;
    gboolean changed = FALSE;

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &in_req);
    priv->cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    cl_int cl_err = CL_SUCCESS;

    #ifdef HAVE_AMD
    clfftDim dimension;
    #else
    clFFT_Dimension dimension;
    #endif

    x_dim = (guint32) in_req.dims[0] / 2;

    switch (priv->fft_dimensions) {
        case FFT_1D:
            dimension = clFFT_1D;
            priv->batch_size = in_req.n_dims == 2 ? (cl_int) in_req.dims[1] : 1;
            break;

        case FFT_2D:
            dimension = clFFT_2D;
            y_dim = (guint32) in_req.dims[1];
            priv->batch_size = in_req.n_dims == 3 ? (cl_int) in_req.dims[2] : 1;
            break;

        case FFT_3D:
            dimension = clFFT_3D;
            break;
    }

    #ifdef HAVE_AMD
    changed = priv->fft_size[0] != x_dim || priv->fft_size[1] != y_dim;
    priv->fft_size[0] = x_dim;
    priv->fft_size[1] = y_dim;
    #else
    changed = priv->fft_size.x != x_dim || priv->fft_size.y != y_dim;
    priv->fft_size.x = x_dim;
    priv->fft_size.y = y_dim;
    #endif

    #ifdef HAVE_AMD
    if (priv->fft_plan == 0 || changed) {
        if (priv->fft_plan != 0) {
            clfftDestroyPlan (&(priv->fft_plan));
            priv->fft_plan = 0;
        }

        cl_err = clfftSetup(&(priv->fft_setup));
        cl_err = clfftCreateDefaultPlan (&(priv->fft_plan), priv->context, dimension, priv->fft_size);
        cl_err = clfftSetPlanBatchSize (priv->fft_plan, priv->batch_size);
        cl_err = clfftSetPlanPrecision (priv->fft_plan, CLFFT_SINGLE);
        cl_err = clfftSetLayout (priv->fft_plan, CLFFT_COMPLEX_INTERLEAVED, CLFFT_COMPLEX_INTERLEAVED);
        cl_err = clfftSetResultLocation (priv->fft_plan, CLFFT_INPLACE);
        cl_err = clfftBakePlan (priv->fft_plan, 1, &(priv->cmd_queue), NULL, NULL);
        UFO_RESOURCES_CHECK_CLERR (cl_err);
    }
    #else
    if (priv->fft_plan == NULL || changed) {
        if (priv->fft_plan != NULL) {
            clFFT_DestroyPlan (priv->fft_plan);
            priv->fft_plan = NULL;
        }

        priv->fft_plan = clFFT_CreatePlan (priv->context, priv->fft_size, dimension, clFFT_InterleavedComplexFormat, &cl_err);

        UFO_RESOURCES_CHECK_CLERR (cl_err);
    }
    #endif

    *requisition = in_req;  // keep third dimension for 2-D batching 
    requisition->dims[0] = priv->crop_width > 0 ? (gsize) priv->crop_width : x_dim;
    requisition->dims[1] = priv->crop_height > 0 ? (gsize) priv->crop_height : in_req.dims[1];
}

static guint
ufo_ifft_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_ifft_task_get_num_dimensions (UfoTask *task,
                                  guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return UFO_IFFT_TASK_GET_PRIVATE (task)->fft_dimensions > 2 ? 3 : 2;
}

static UfoTaskMode
ufo_ifft_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_ifft_task_equal_real (UfoNode *n1,
                          UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_IFFT_TASK (n1) && UFO_IS_IFFT_TASK (n2), FALSE);
    return TRUE;
}

static gboolean
ufo_ifft_task_process (UfoTask *task,
                       UfoBuffer **inputs,
                       UfoBuffer *output,
                       UfoRequisition *requisition)
{
    UfoIfftTaskPrivate *priv;
    #ifndef HAVE_AMD
    UfoProfiler *profiler;
    #endif
    UfoRequisition in_req;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_int width;
    cl_int height;
    cl_int x_dim;
    gfloat scale;
    gsize global_work_size[2];

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    #ifndef HAVE_AMD
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    #endif
    in_mem = ufo_buffer_get_device_array (inputs[0], priv->cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, priv->cmd_queue);

    #ifdef HAVE_AMD
    clfftEnqueueTransform (priv->fft_plan,
                           CLFFT_BACKWARD, 1, &(priv->cmd_queue),
                           0, NULL, NULL,
                           &in_mem, &in_mem, NULL);
    #else
    clFFT_ExecuteInterleaved_Ufo (priv->cmd_queue,
                                  priv->fft_plan, priv->batch_size, clFFT_Inverse,
                                  in_mem, in_mem,
                                  0, NULL, NULL, profiler);
    #endif

    scale = 1.0f / ((gfloat) requisition->dims[0]);

    if (priv->fft_dimensions == FFT_2D) {
        scale /= (gfloat) requisition->dims[1];
    }

    width = priv->crop_width > 0 ? priv->crop_width : (cl_int) requisition->dims[0];
    height = (cl_int) requisition->dims[1];
    x_dim = (cl_int) requisition->dims[0];

    ufo_buffer_get_requisition (inputs[0], &in_req);

    global_work_size[0] = requisition->n_dims <=2 ? in_req.dims[0] >> 1 
                            : (in_req.dims[0] * in_req.dims[2]) >> 1;
    global_work_size[1] = height;

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), (gpointer) &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), (gpointer) &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_int), &width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (cl_int), &x_dim));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 4, sizeof (gfloat), &scale));

    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (priv->cmd_queue,
                                                       priv->kernel,
                                                       2, NULL, global_work_size, NULL,
                                                       0, NULL, NULL));
    return TRUE;
}

static void
ufo_ifft_task_finalize (GObject *object)
{
    UfoIfftTaskPrivate *priv;

    priv = UFO_IFFT_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    #ifdef HAVE_AMD
    clfftDestroyPlan (&(priv->fft_plan));
    //clfftTeardown ();
    #else
    if (priv->fft_plan != NULL) {
        clFFT_DestroyPlan (priv->fft_plan);
    }
    #endif

    G_OBJECT_CLASS (ufo_ifft_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_ifft_task_setup;
    iface->get_requisition = ufo_ifft_task_get_requisition;
    iface->get_num_inputs = ufo_ifft_task_get_num_inputs;
    iface->get_num_dimensions = ufo_ifft_task_get_num_dimensions;
    iface->get_mode = ufo_ifft_task_get_mode;
    iface->process = ufo_ifft_task_process;
}

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
        case PROP_CROP_HEIGHT:
            priv->crop_height = g_value_get_int (value);
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
        case PROP_CROP_HEIGHT:
            g_value_set_int (value, priv->crop_height);
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

    properties[PROP_CROP_HEIGHT] =
        g_param_spec_int ("crop-height",
                          "Height of cropped output",
                          "Height of cropped output",
                          -1, G_MAXINT, -1,
                          G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->equal = ufo_ifft_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoIfftTaskPrivate));
}

static void
ufo_ifft_task_init (UfoIfftTask *self)
{
    UfoIfftTaskPrivate *priv;
    self->priv = priv = UFO_IFFT_TASK_GET_PRIVATE (self);
    priv->crop_width = -1;
    priv->crop_height = -1;
    priv->batch_size = 1;
    priv->fft_dimensions = FFT_1D;

    #ifdef HAVE_AMD
    priv->fft_size[0] = 1;
    priv->fft_size[1] = 1;
    priv->fft_size[2] = 1;
    priv->fft_setup = (clfftSetupData){0,0,0,0};
    priv->fft_plan = 0;
    #else
    priv->fft_size.x = 1;
    priv->fft_size.y = 1;
    priv->fft_size.z = 1;
    priv->fft_plan = NULL;
    #endif

    priv->kernel = NULL;
    priv->context = NULL;
}
