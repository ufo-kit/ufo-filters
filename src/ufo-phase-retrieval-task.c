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

#include "ufo-phase-retrieval-task.h"

#define IS_POW_OF_2(x) !(x & (x - 1))

typedef enum {
    METHOD_TIE,
    METHOD_CTF,
    METHOD_CTFHALFSINE,
    METHOD_QP,
    METHOD_QPHALFSINE,
    METHOD_QP2,
    N_METHODS
} Method;

struct _UfoPhaseRetrievalTaskPrivate {
    Method method;
    guint width;
    guint height;
    gfloat energy;
    gfloat distance;
    gfloat pixel_size;
    gfloat regularization_rate;
    gfloat binary_filter;

    gfloat prefac;
    gint normalize;
    gfloat sub_value;
    cl_kernel *kernels;
    cl_kernel mult_by_value_kernel;
    cl_kernel sub_value_kernel;
    cl_kernel get_real_kernel;
    cl_context context;
    cl_command_queue cmd_queue;
    #ifdef HAVE_AMD
    clfftPlanHandle fft_plan;
    clfftSetupData fft_setup;
    size_t fft_size[3];
    #else
    clFFT_Plan fft_plan;
    clFFT_Dim3 fft_size;
    #endif
    UfoBuffer *fft_buffer;
    UfoBuffer *filter_buffer;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoPhaseRetrievalTask, ufo_phase_retrieval_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_PHASE_RETRIEVAL_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_PHASE_RETRIEVAL_TASK, UfoPhaseRetrievalTaskPrivate))


enum {
    PROP_0,
    PROP_METHOD,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_ENERGY,
    PROP_DISTANCE,
    PROP_PIXEL_SIZE,
    PROP_REGULARIZATION_RATE,
    PROP_BINARY_FILTER_THRESHOLDING,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_phase_retrieval_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_PHASE_RETRIEVAL_TASK, NULL));
}

static void
ufo_phase_retrieval_task_setup (UfoTask *task,
                                UfoResources *resources,
                                GError **error)
{
    UfoPhaseRetrievalTaskPrivate *priv;
    UfoGpuNode *node;
    gfloat lambda;

    priv = UFO_PHASE_RETRIEVAL_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));

    priv->context = ufo_resources_get_context (resources);
    priv->cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    lambda = 6.62606896e-34 * 299792458 / (priv->energy * 1.60217733e-16);
    priv->prefac = 2 * G_PI * lambda * priv->distance / (priv->pixel_size * priv->pixel_size);

    priv->kernels[METHOD_TIE] = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "tie_method", error);
    priv->kernels[METHOD_CTF] = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "ctf_method", error);
    priv->kernels[METHOD_CTFHALFSINE] = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "ctfhalfsine_method", error);
    priv->kernels[METHOD_QP] = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "qp_method", error);
    priv->kernels[METHOD_QPHALFSINE] = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "qphalfsine_method", error);
    priv->kernels[METHOD_QP2] = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "qp2_method", error);

    priv->sub_value_kernel = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "subtract_value", error);
    priv->mult_by_value_kernel = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "mult_by_value", error);
    priv->get_real_kernel = ufo_resources_get_kernel(resources, "phase_retrieval.cl", "get_real", error);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext(priv->context));

    if (priv->fft_buffer == NULL) {
        UfoRequisition requisition;
        requisition.n_dims = 2;
        requisition.dims[0] = 1;
        requisition.dims[1] = 1;

        priv->fft_buffer = ufo_buffer_new(&requisition, priv->context);
    }

    if (priv->filter_buffer == NULL) {
        UfoRequisition requisition;
        requisition.n_dims = 2;
        requisition.dims[0] = 1;
        requisition.dims[1] = 1;

        priv->filter_buffer = ufo_buffer_new(&requisition, priv->context);
    }


    for (int i = 0; i < N_METHODS; i++) {
        if (priv->kernels[i] != NULL) {
            UFO_RESOURCES_CHECK_CLERR (clRetainKernel(priv->kernels[i]));
        }
    }

    if (priv->sub_value_kernel != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->sub_value_kernel));
    }

    if (priv->mult_by_value_kernel != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->mult_by_value_kernel));
    }

    if (priv->get_real_kernel != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->get_real_kernel));
    }
}

static void
ufo_phase_retrieval_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoPhaseRetrievalTaskPrivate *priv;
    UfoRequisition input_requisition;
    cl_int cl_err;

    priv = UFO_PHASE_RETRIEVAL_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &input_requisition);

    requisition->n_dims = 2;
    requisition->dims[0] = input_requisition.dims[0];
    requisition->dims[1] = input_requisition.dims[1];

    if (!IS_POW_OF_2(requisition->dims[0]) || !IS_POW_OF_2(requisition->dims[1])) {
        g_error("Please, perform zeropadding of your dataset along both directions (width, height) up to length of power of 2 (e.g. 256, 512, 1024, 2048, etc.)");
        return;
    }

    #ifdef HAVE_AMD
    if (priv->fft_plan == 0) {
        priv->fft_size[0] = input_requisition.dims[0];
        priv->fft_size[1] = input_requisition.dims[1];
        priv->fft_size[2] = 1;

        cl_err = clfftSetup(&(priv->fft_setup));
        cl_err = clfftCreateDefaultPlan (&(priv->fft_plan), priv->context, CLFFT_2D, priv->fft_size);
        cl_err = clfftSetPlanBatchSize (priv->fft_plan, 1);
        cl_err = clfftSetPlanPrecision (priv->fft_plan, CLFFT_SINGLE);
        cl_err = clfftSetLayout (priv->fft_plan, CLFFT_COMPLEX_INTERLEAVED, CLFFT_COMPLEX_INTERLEAVED);
        cl_err = clfftSetResultLocation (priv->fft_plan, CLFFT_INPLACE);
        cl_err = clfftBakePlan (priv->fft_plan, 1, &(priv->cmd_queue), NULL, NULL);
    #else
    if (priv->fft_plan == NULL) {
        priv->fft_size.x = input_requisition.dims[0];
        priv->fft_size.y = input_requisition.dims[1];
        priv->fft_size.z = 1;

        priv->fft_plan = clFFT_CreatePlan (priv->context,
                                           priv->fft_size,
                                           clFFT_2D,
                                           clFFT_InterleavedComplexFormat, 
                                           &cl_err);
    #endif


        UFO_RESOURCES_CHECK_CLERR (cl_err);
    }
}

static guint
ufo_filter_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_filter_task_get_num_dimensions (UfoTask *task, guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_filter_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_phase_retrieval_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoPhaseRetrievalTaskPrivate *priv;
    UfoProfiler *profiler;

    cl_mem in_mem, out_mem, fft_mem, filter_mem;
    cl_kernel method_kernel;

    priv = UFO_PHASE_RETRIEVAL_TASK_GET_PRIVATE (task);
    out_mem = ufo_buffer_get_device_array (output, priv->cmd_queue);
    in_mem = ufo_buffer_get_device_array (inputs[0], priv->cmd_queue);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
 
    UfoRequisition fft_requisition;
    fft_requisition.n_dims = 2;
    fft_requisition.dims[0] = requisition->dims[0] * 2;
    fft_requisition.dims[1] = requisition->dims[1];
    ufo_buffer_resize(priv->fft_buffer, &fft_requisition);
    fft_mem = ufo_buffer_get_device_array (priv->fft_buffer, priv->cmd_queue);

    if (ufo_buffer_cmp_dimensions(priv->filter_buffer, requisition) != 0) {
        ufo_buffer_resize(priv->filter_buffer, requisition);
        filter_mem = ufo_buffer_get_device_array (priv->filter_buffer, priv->cmd_queue);

        method_kernel = priv->kernels[(gint)priv->method];

        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 0, sizeof (gint), &priv->normalize));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 1, sizeof (gfloat), &priv->prefac));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 2, sizeof (gfloat), &priv->regularization_rate));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 3, sizeof (gfloat), &priv->binary_filter));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 4, sizeof (cl_mem), &filter_mem));
        ufo_profiler_call (profiler, priv->cmd_queue, method_kernel, requisition->n_dims, requisition->dims, NULL);
    }
    else {
        filter_mem = ufo_buffer_get_device_array (priv->filter_buffer, priv->cmd_queue);
    }

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sub_value_kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sub_value_kernel, 1, sizeof (cl_mem), &fft_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sub_value_kernel, 2, sizeof (gfloat), &priv->sub_value));
    ufo_profiler_call (profiler, priv->cmd_queue, priv->sub_value_kernel, requisition->n_dims, requisition->dims, NULL);

    #ifdef HAVE_AMD
    clfftEnqueueTransform (priv->fft_plan, 
                           CLFFT_FORWARD, 1, &(priv->cmd_queue),
                           0, NULL, NULL, 
                           &fft_mem, &fft_mem, NULL);
    #else
    clFFT_ExecuteInterleaved_Ufo (priv->cmd_queue, priv->fft_plan, 1, clFFT_Forward, fft_mem, fft_mem, 0, NULL, NULL, profiler);
    #endif

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mult_by_value_kernel, 0, sizeof (cl_mem), &fft_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mult_by_value_kernel, 1, sizeof (cl_mem), &filter_mem));
    ufo_profiler_call (profiler, priv->cmd_queue, priv->mult_by_value_kernel, requisition->n_dims, requisition->dims, NULL);

    #ifdef HAVE_AMD
    clfftEnqueueTransform (priv->fft_plan, 
                           CLFFT_BACKWARD, 1, &(priv->cmd_queue),
                           0, NULL, NULL, 
                           &fft_mem, &fft_mem, NULL);
    #else
    clFFT_ExecuteInterleaved_Ufo (priv->cmd_queue, priv->fft_plan, 1, clFFT_Inverse, fft_mem, fft_mem, 0, NULL, NULL, profiler);
    #endif

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->get_real_kernel, 0, sizeof (cl_mem), &fft_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->get_real_kernel, 1, sizeof (cl_mem), &out_mem));
    ufo_profiler_call (profiler, priv->cmd_queue, priv->get_real_kernel, requisition->n_dims, requisition->dims, NULL);

    return TRUE;
}

static void
ufo_phase_retrieval_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoPhaseRetrievalTaskPrivate *priv = UFO_PHASE_RETRIEVAL_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_METHOD:
            if (priv->method == METHOD_TIE)
                g_value_set_string (value, "tie");
            else if (priv->method == METHOD_CTF)
                g_value_set_string (value, "ctf");
            else if (priv->method == METHOD_CTFHALFSINE)
                g_value_set_string (value, "ctfhalfsine");
            else if (priv->method == METHOD_QP)
                g_value_set_string (value, "qp");
            else if (priv->method == METHOD_QPHALFSINE)
                g_value_set_string (value, "qphalfsine");
            else if (priv->method == METHOD_QP2)
                g_value_set_string (value, "qp2");
            break;
        case PROP_WIDTH:
            g_value_set_uint (value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint (value, priv->height);
            break;
        case PROP_ENERGY:
            g_value_set_float (value, priv->energy);
            break;
        case PROP_DISTANCE:
            g_value_set_float (value, priv->distance);
            break;
        case PROP_PIXEL_SIZE:
            g_value_set_float (value, priv->pixel_size);
            break;
        case PROP_REGULARIZATION_RATE:
            g_value_set_float (value, priv->regularization_rate);
            break;
        case PROP_BINARY_FILTER_THRESHOLDING:
            g_value_set_float (value, priv->binary_filter);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_phase_retrieval_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoPhaseRetrievalTaskPrivate *priv = UFO_PHASE_RETRIEVAL_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_METHOD:
            if (!g_strcmp0 (g_value_get_string (value), "tie"))
                priv->method = METHOD_TIE;
            else if (!g_strcmp0 (g_value_get_string (value), "ctf"))
                priv->method = METHOD_CTF;
            else if (!g_strcmp0 (g_value_get_string (value), "ctfhalfsine"))
                priv->method = METHOD_CTFHALFSINE;
            else if (!g_strcmp0 (g_value_get_string (value), "qp"))
                priv->method = METHOD_QP;
            else if (!g_strcmp0 (g_value_get_string (value), "qphalfsine"))
                priv->method = METHOD_QPHALFSINE;
            else if (!g_strcmp0 (g_value_get_string (value), "qp2"))
                priv->method = METHOD_QP2;
            break;
        case PROP_WIDTH:
            priv->width = g_value_get_uint (value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint (value);
            break;
        case PROP_ENERGY:
            priv->energy = g_value_get_float (value);
            break;
        case PROP_DISTANCE:
            priv->distance = g_value_get_float (value);
            break;
        case PROP_PIXEL_SIZE:
            priv->pixel_size = g_value_get_float (value);
            break;
        case PROP_REGULARIZATION_RATE:
            priv->regularization_rate = g_value_get_float (value);
            break;
        case PROP_BINARY_FILTER_THRESHOLDING:
            priv->binary_filter = g_value_get_float (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }

}

static void
ufo_phase_retrieval_task_finalize (GObject *object)
{
    UfoPhaseRetrievalTaskPrivate *priv;

    priv = UFO_PHASE_RETRIEVAL_TASK_GET_PRIVATE (object);

    #ifdef HAVE_AMD
    clfftDestroyPlan (&(priv->fft_plan));
    //clfftTeardown ();
    #else
    clFFT_DestroyPlan (priv->fft_plan);
    #endif

    if (priv->kernels) {
        for (int i = 0; i < N_METHODS; i++) {
            UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernels[i]));
            priv->kernels[i] = NULL;
        }
    }

    g_free(priv->kernels);

    if (priv->mult_by_value_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->mult_by_value_kernel));
        priv->mult_by_value_kernel = NULL;
    }

    if (priv->sub_value_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->sub_value_kernel));
        priv->sub_value_kernel = NULL;
    }

    if (priv->get_real_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->get_real_kernel));
        priv->sub_value_kernel = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    if (priv->fft_buffer) {
        g_object_unref(priv->fft_buffer);
    }

    if (priv->filter_buffer) {
        g_object_unref(priv->filter_buffer);
    }
    
    G_OBJECT_CLASS (ufo_phase_retrieval_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_phase_retrieval_task_setup;
    iface->get_requisition = ufo_phase_retrieval_task_get_requisition;
    iface->get_num_inputs = ufo_filter_task_get_num_inputs;
    iface->get_num_dimensions = ufo_filter_task_get_num_dimensions;
    iface->get_mode = ufo_filter_task_get_mode;
    iface->process = ufo_phase_retrieval_task_process;
}

static void
ufo_phase_retrieval_task_class_init (UfoPhaseRetrievalTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_phase_retrieval_task_set_property;
    gobject_class->get_property = ufo_phase_retrieval_task_get_property;
    gobject_class->finalize = ufo_phase_retrieval_task_finalize;

    properties[PROP_METHOD] =
        g_param_spec_string ("method",
            "Name of method",
            "Method.",
            "tie",
            G_PARAM_READWRITE);

    properties[PROP_WIDTH] =
        g_param_spec_uint ("width",
            "Filter width",
            "Width of filter.",
            1, G_MAXUINT, 1024,
            G_PARAM_READWRITE);

    properties[PROP_HEIGHT] =
        g_param_spec_uint ("height",
            "Filter height",
            "Height of filter.",
            1, G_MAXUINT, 1024,
            G_PARAM_READWRITE);

    properties[PROP_ENERGY] =
        g_param_spec_float ("energy",
            "Energy value",
            "Energy value.",
            0, G_MAXFLOAT, 20,
            G_PARAM_READWRITE);

    properties[PROP_DISTANCE] =
        g_param_spec_float ("distance",
            "Distance value",
            "Distance value.",
            0, G_MAXFLOAT, 0.945,
            G_PARAM_READWRITE);

    properties[PROP_PIXEL_SIZE] =
        g_param_spec_float ("pixel-size",
            "Pixel size",
            "Pixel size.",
            0, G_MAXFLOAT, 0.75e-6,
            G_PARAM_READWRITE);

    properties[PROP_REGULARIZATION_RATE] =
        g_param_spec_float ("regularization-rate",
            "Regularization rate value",
            "Regularization rate value.",
            0, G_MAXFLOAT, 2.5,
            G_PARAM_READWRITE);

    properties[PROP_BINARY_FILTER_THRESHOLDING] =
        g_param_spec_float ("thresholding-rate",
            "Binary thresholding rate value",
            "Binary thresholding rate value.",
            0, G_MAXFLOAT, 0.1,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoPhaseRetrievalTaskPrivate));
}

static void
ufo_phase_retrieval_task_init(UfoPhaseRetrievalTask *self)
{
    UfoPhaseRetrievalTaskPrivate *priv;
    self->priv = priv = UFO_PHASE_RETRIEVAL_TASK_GET_PRIVATE(self);
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
    priv->method = METHOD_TIE;
    priv->width = 1024;
    priv->height = 1024;
    priv->energy = 20.0f;
    priv->distance = 0.945f;
    priv->pixel_size = 0.75e-6f;
    priv->regularization_rate = 2.5f;
    priv->binary_filter = 0.1f;
    priv->normalize = 1;
    priv->sub_value = 1.0f;
    priv->kernels = (cl_kernel *) g_malloc0(N_METHODS * sizeof(cl_kernel));
    priv->fft_buffer = NULL;
    priv->filter_buffer = NULL;
}
