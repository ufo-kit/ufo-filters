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

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-retrieve-phase-task.h"

#define IS_POW_OF_2(x) !(x & (x - 1))

typedef enum {
    METHOD_TIE = 0,
    METHOD_CTF,
    METHOD_CTFHALFSINE,
    METHOD_QP,
    METHOD_QPHALFSINE,
    METHOD_QP2,
    N_METHODS
} Method;

static GEnumValue method_values[] = {
    { METHOD_TIE,           "METHOD_TIE",           "tie" },
    { METHOD_CTF,           "METHOD_CTF",           "ctf" },
    { METHOD_CTFHALFSINE,   "METHOD_CTFHALFSINE",   "ctfhalfsine" },
    { METHOD_QP,            "METHOD_QP",            "qp" },
    { METHOD_QPHALFSINE,    "METHOD_QPHALFSINE",    "qphalfsine" },
    { METHOD_QP2,           "METHOD_QP2",           "qp2" },
    { 0, NULL, NULL}
};

struct _UfoRetrievePhaseTaskPrivate {
    Method method;
    gfloat energy;
    gfloat distance;
    gfloat pixel_size;
    gfloat regularization_rate;
    gfloat binary_filter;

    gfloat prefac;
    cl_kernel *kernels;
    cl_kernel mult_by_value_kernel;
    cl_context context;
    UfoBuffer *filter_buffer;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRetrievePhaseTask, ufo_retrieve_phase_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_RETRIEVE_PHASE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_RETRIEVE_PHASE_TASK, UfoRetrievePhaseTaskPrivate))


enum {
    PROP_0,
    PROP_METHOD,
    PROP_ENERGY,
    PROP_DISTANCE,
    PROP_PIXEL_SIZE,
    PROP_REGULARIZATION_RATE,
    PROP_BINARY_FILTER_THRESHOLDING,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_retrieve_phase_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_RETRIEVE_PHASE_TASK, NULL));
}

static void
ufo_retrieve_phase_task_setup (UfoTask *task,
                               UfoResources *resources,
                               GError **error)
{
    UfoRetrievePhaseTaskPrivate *priv;
    gfloat lambda;

    priv = UFO_RETRIEVE_PHASE_TASK_GET_PRIVATE (task);
    priv->context = ufo_resources_get_context (resources);

    lambda = 6.62606896e-34 * 299792458 / (priv->energy * 1.60217733e-16);
    priv->prefac = 2 * G_PI * lambda * priv->distance / (priv->pixel_size * priv->pixel_size);

    priv->kernels[METHOD_TIE] = ufo_resources_get_kernel(resources, "phase-retrieval.cl", "tie_method", error);
    priv->kernels[METHOD_CTF] = ufo_resources_get_kernel(resources, "phase-retrieval.cl", "ctf_method", error);
    priv->kernels[METHOD_CTFHALFSINE] = ufo_resources_get_kernel(resources, "phase-retrieval.cl", "ctfhalfsine_method", error);
    priv->kernels[METHOD_QP] = ufo_resources_get_kernel(resources, "phase-retrieval.cl", "qp_method", error);
    priv->kernels[METHOD_QPHALFSINE] = ufo_resources_get_kernel(resources, "phase-retrieval.cl", "qphalfsine_method", error);
    priv->kernels[METHOD_QP2] = ufo_resources_get_kernel(resources, "phase-retrieval.cl", "qp2_method", error);

    priv->mult_by_value_kernel = ufo_resources_get_kernel(resources, "phase-retrieval.cl", "mult_by_value", error);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext(priv->context));

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

    if (priv->mult_by_value_kernel != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->mult_by_value_kernel));
    }
}

static void
ufo_retrieve_phase_task_get_requisition (UfoTask *task,
                                         UfoBuffer **inputs,
                                         UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);

    if (!IS_POW_OF_2 (requisition->dims[0]) || !IS_POW_OF_2 (requisition->dims[1])) {
        g_error("Please, perform zeropadding of your dataset along both directions (width, height) up to length of power of 2 (e.g. 256, 512, 1024, 2048, etc.)");
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
ufo_retrieve_phase_task_process (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoBuffer *output,
                                 UfoRequisition *requisition)
{
    UfoRetrievePhaseTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;

    cl_mem in_mem, out_mem, filter_mem;
    cl_kernel method_kernel;
    cl_command_queue cmd_queue;

    priv = UFO_RETRIEVE_PHASE_TASK_GET_PRIVATE (task);

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    if (ufo_buffer_cmp_dimensions (priv->filter_buffer, requisition) != 0) {
        ufo_buffer_resize (priv->filter_buffer, requisition);
        filter_mem = ufo_buffer_get_device_array (priv->filter_buffer, cmd_queue);

        method_kernel = priv->kernels[(gint)priv->method];

        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 0, sizeof (gfloat), &priv->prefac));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 1, sizeof (gfloat), &priv->regularization_rate));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 2, sizeof (gfloat), &priv->binary_filter));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (method_kernel, 3, sizeof (cl_mem), &filter_mem));
        ufo_profiler_call (profiler, cmd_queue, method_kernel, requisition->n_dims, requisition->dims, NULL);
    }
    else {
        filter_mem = ufo_buffer_get_device_array (priv->filter_buffer, cmd_queue);
    }

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mult_by_value_kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mult_by_value_kernel, 1, sizeof (cl_mem), &filter_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mult_by_value_kernel, 2, sizeof (cl_mem), &out_mem));
    ufo_profiler_call (profiler, cmd_queue, priv->mult_by_value_kernel, requisition->n_dims, requisition->dims, NULL);
    
    return TRUE;
}

static void
ufo_retrieve_phase_task_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
    UfoRetrievePhaseTaskPrivate *priv = UFO_RETRIEVE_PHASE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_METHOD:
            g_value_set_enum (value, priv->method);
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
ufo_retrieve_phase_task_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
    UfoRetrievePhaseTaskPrivate *priv = UFO_RETRIEVE_PHASE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_METHOD:
            priv->method = g_value_get_enum (value);
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
ufo_retrieve_phase_task_finalize (GObject *object)
{
    UfoRetrievePhaseTaskPrivate *priv;

    priv = UFO_RETRIEVE_PHASE_TASK_GET_PRIVATE (object);

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

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    if (priv->filter_buffer) {
        g_object_unref(priv->filter_buffer);
    }

    G_OBJECT_CLASS (ufo_retrieve_phase_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_retrieve_phase_task_setup;
    iface->get_requisition = ufo_retrieve_phase_task_get_requisition;
    iface->get_num_inputs = ufo_filter_task_get_num_inputs;
    iface->get_num_dimensions = ufo_filter_task_get_num_dimensions;
    iface->get_mode = ufo_filter_task_get_mode;
    iface->process = ufo_retrieve_phase_task_process;
}

static void
ufo_retrieve_phase_task_class_init (UfoRetrievePhaseTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_retrieve_phase_task_set_property;
    gobject_class->get_property = ufo_retrieve_phase_task_get_property;
    gobject_class->finalize = ufo_retrieve_phase_task_finalize;

    properties[PROP_METHOD] =
        g_param_spec_enum ("method",
            "Method name",
            "Method name",
            g_enum_register_static ("method", method_values),
            METHOD_TIE,
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

    g_type_class_add_private (gobject_class, sizeof(UfoRetrievePhaseTaskPrivate));
}

static void
ufo_retrieve_phase_task_init(UfoRetrievePhaseTask *self)
{
    UfoRetrievePhaseTaskPrivate *priv;
    self->priv = priv = UFO_RETRIEVE_PHASE_TASK_GET_PRIVATE(self);
    priv->method = METHOD_TIE;
    priv->energy = 20.0f;
    priv->distance = 0.945f;
    priv->pixel_size = 0.75e-6f;
    priv->regularization_rate = 2.5f;
    priv->binary_filter = 0.1f;
    priv->kernels = (cl_kernel *) g_malloc0(N_METHODS * sizeof(cl_kernel));
    priv->filter_buffer = NULL;
}
