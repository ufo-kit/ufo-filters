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
#include <math.h>

#include "ufo-filter-task.h"

/**
 * SECTION:ufo-filter-task
 * @Short_description: Apply one-dimensional ramp frequency filter
 * @Title: filter
 *
 * Applies the ramp filter for preparing a sinogram to be processed by the
 * backprojection node. A particular filter can be choosen with the
 * #UfoFilterTask:filter property.
 */

typedef void (*SetupFunc)(UfoFilterTaskPrivate *priv, gfloat *coefficients, guint width);

static void ufo_task_interface_init (UfoTaskIface *iface);
static void compute_ramp_coefficients (UfoFilterTaskPrivate *, gfloat *, guint);
static void compute_butterworth_coefficients (UfoFilterTaskPrivate *, gfloat *, guint);
static void compute_faris_byer_coefficients (UfoFilterTaskPrivate *, gfloat *, guint);
static void compute_hamming_coefficients (UfoFilterTaskPrivate *, gfloat *, guint);

typedef enum {
    FILTER_RAMP = 0,
    FILTER_BUTTERWORTH,
    FILTER_FARIS_BYER,
    FILTER_HAMMING,
    N_FILTERS,
} Filter;

static const gchar *filter_names[] = {
    "ramp",
    "butterworth",
    "faris-byer",
    "hamming",
};

static SetupFunc filter_funcs[] = {
    &compute_ramp_coefficients,
    &compute_butterworth_coefficients,
    &compute_faris_byer_coefficients,
    &compute_hamming_coefficients,
};

struct _UfoFilterTaskPrivate {
    cl_context context;
    cl_kernel kernel;
    cl_mem filter_mem;
    gfloat cutoff;
    gfloat bw_order;
    gfloat fb_tau;
    gfloat fb_theta;
    gfloat scale;
    Filter filter;
};

G_DEFINE_TYPE_WITH_CODE (UfoFilterTask, ufo_filter_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_FILTER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_TASK, UfoFilterTaskPrivate))

enum {
    PROP_0,
    PROP_FILTER,
    PROP_CUTOFF,
    PROP_BW_ORDER,
    PROP_FB_TAU,
    PROP_FB_THETA,
    PROP_SCALE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_filter_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FILTER_TASK, NULL));
}

static gboolean
ufo_filter_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoFilterTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;

    priv = UFO_FILTER_TASK (task)->priv;
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_mem), &priv->filter_mem));

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, priv->kernel, 2, requisition->dims, NULL);

    return TRUE;
}

static void
ufo_filter_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoFilterTaskPrivate *priv;

    priv = UFO_FILTER_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    priv->kernel = ufo_resources_get_kernel (resources, "filter.cl", "filter", error);

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
}

static void
mirror_coefficients (gfloat *filter, guint width)
{
    for (guint k = width/2 + 2; k < width; k += 2) {
        filter[k] = filter[width - k];
        filter[k + 1] = filter[width - k + 1];
    }
}

static void
compute_ramp_coefficients (UfoFilterTaskPrivate *priv,
                           gfloat *filter,
                           guint width)
{
    const gdouble step = 2.0 / width;

    for (guint k = 1; k < width / 4 + 1; k++) {
        filter[2*k] = k * step * priv->scale;
        filter[2*k + 1] = filter[2*k];
    }
}

static void
compute_butterworth_coefficients (UfoFilterTaskPrivate *priv,
                                  gfloat *filter,
                                  guint width)
{
    const gdouble step = 2.0 / width;

    for (guint k = 0; k < (width / 4) + 1; k++) {
        const gdouble f = k * step;
        filter[2*k] = (gfloat) (f / (1.0 + pow (f / priv->cutoff, 2.0 * priv->bw_order)) * priv->scale);
        filter[2*k+1] = filter[2*k];
    }
}

static void
compute_hamming_coefficients (UfoFilterTaskPrivate *priv,
                              gfloat *filter,
                              guint width)
{
    const gdouble step = 2.0 / width;

    for (guint k = 0; k < (width / 4) + 1; k++) {
        const gdouble f = k * step;

        filter[2*k] = f < priv->cutoff ? f * (0.54 + 0.46 * cos (G_PI * f / priv->cutoff)) * priv->scale : 0;
        filter[2*k+1] = filter[2*k];
    }
}

static guint
get_padding_value (guint x)
{
    guint padding = 2 * x;
    guint result = 1;

    while (result < padding)
        result *= 2;

    return result;
}

static void
compute_faris_byer_coefficients (UfoFilterTaskPrivate *priv,
                                 gfloat *filter,
                                 guint width)
{
    const gdouble pi_squared_tau = G_PI * G_PI * priv->fb_tau;
    const gdouble sin_theta_2 = - sin (priv->fb_theta) / 2;
    const guint padding = get_padding_value (width);

    filter[0] = 0;

    for (guint x = 1; x <= width / 2; x++) {
        if (x % 2 != 0)
            filter[x] = 1 / (pi_squared_tau * x);
    }

    for (guint i = width / 2 + 1; i < width; i++) {
        guint x = width + 1 - i;

        if (x % 2 != 0)
            filter[padding - width - i - 1] = sin_theta_2 / (x * x * pi_squared_tau);
    }
}

static void
ufo_filter_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoFilterTaskPrivate *priv;

    priv = UFO_FILTER_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], requisition);

    if (priv->filter_mem == NULL) {
        cl_int cl_err;
        guint width;
        gfloat *coefficients;

        width = (guint) requisition->dims[0];
        coefficients = g_malloc0 (width * sizeof (gfloat));

        coefficients[0] = 0.5 / width;
        coefficients[1] = coefficients[0];

        filter_funcs[priv->filter] (priv, coefficients, width);
        mirror_coefficients (coefficients, width);

        priv->filter_mem = clCreateBuffer (priv->context,
                                           CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                           width * sizeof(float),
                                           coefficients,
                                           &cl_err);
        UFO_RESOURCES_CHECK_CLERR (cl_err);
        g_free (coefficients);
    }
}

static guint
ufo_filter_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_filter_task_get_num_dimensions (UfoTask *task,
                               guint input)
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
ufo_filter_task_equal_real (UfoNode *n1,
                            UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_FILTER_TASK (n1) && UFO_IS_FILTER_TASK (n2), FALSE);
    return TRUE;
}

static void
ufo_filter_task_finalize (GObject *object)
{
    UfoFilterTaskPrivate *priv;

    priv = UFO_FILTER_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        clReleaseKernel (priv->kernel);
        priv->kernel = NULL;
    }

    if (priv->filter_mem) {
        clReleaseMemObject (priv->filter_mem);
        priv->filter_mem = NULL;
    }

    G_OBJECT_CLASS (ufo_filter_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_filter_task_setup;
    iface->get_requisition = ufo_filter_task_get_requisition;
    iface->get_num_inputs = ufo_filter_task_get_num_inputs;
    iface->get_num_dimensions = ufo_filter_task_get_num_dimensions;
    iface->get_mode = ufo_filter_task_get_mode;
    iface->process = ufo_filter_task_process;
}

static void
ufo_filter_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoFilterTaskPrivate *priv = UFO_FILTER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FILTER:
            for (guint i = 0; i < N_FILTERS; i++) {
                if (!g_strcmp0 (g_value_get_string (value), filter_names[i])) {
                    priv->filter = (Filter) i;
                    break;
                }
            }
            break;
        case PROP_CUTOFF:
            priv->cutoff = g_value_get_float (value);
            break;
        case PROP_BW_ORDER:
            priv->bw_order = g_value_get_float (value);
            break;
        case PROP_FB_TAU:
            priv->fb_tau = g_value_get_float (value);
            break;
        case PROP_FB_THETA:
            priv->fb_theta = g_value_get_float (value);
            break;
        case PROP_SCALE:
            priv->scale = g_value_get_float (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoFilterTaskPrivate *priv = UFO_FILTER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FILTER:
            g_value_set_string (value, filter_names[priv->filter]);
            break;
        case PROP_CUTOFF:
            g_value_set_float (value, priv->cutoff);
            break;
        case PROP_BW_ORDER:
            g_value_set_float (value, priv->bw_order);
            break;
        case PROP_FB_TAU:
            g_value_set_float (value, priv->fb_tau);
            break;
        case PROP_FB_THETA:
            g_value_set_float (value, priv->fb_theta);
            break;
        case PROP_SCALE:
            g_value_set_float (value, priv->scale);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_task_class_init (UfoFilterTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;

    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->finalize = ufo_filter_task_finalize;
    oclass->set_property = ufo_filter_task_set_property;
    oclass->get_property = ufo_filter_task_get_property;

    properties[PROP_FILTER] =
        g_param_spec_string ("filter",
            "Type of filter (\"ramp\", \"butterworth\", \"faris-byer\", \"hamming\")",
            "Type of filter (\"ramp\", \"butterworth\", \"faris-byer\", \"hamming\")",
            "ramp",
            G_PARAM_READWRITE);

    properties[PROP_CUTOFF] =
        g_param_spec_float ("cutoff",
            "Relative cutoff frequency",
            "Relative cutoff frequency",
            0.0f, 1.0f, 0.5f,
            G_PARAM_READWRITE);

    properties[PROP_BW_ORDER] =
        g_param_spec_float ("order",
            "Order of the Butterworth filter",
            "Order of the Butterworth filter",
            2.0f, 32.0f, 4.0f,
            G_PARAM_READWRITE);

    properties[PROP_FB_TAU] =
        g_param_spec_float ("tau",
            "Tau parameter for Faris-Byer filter",
            "Tau parameter for Faris-Byer filter",
            -G_MAXFLOAT, G_MAXFLOAT, 1.0,
            G_PARAM_READWRITE);

    properties[PROP_FB_THETA] =
        g_param_spec_float ("theta",
            "Theta parameter for Faris-Byer filter",
            "Theta parameter for Faris-Byer filter",
            -G_MAXFLOAT, G_MAXFLOAT, 1.0,
            G_PARAM_READWRITE);

    properties[PROP_SCALE] =
        g_param_spec_float ("scale",
            "Every component is multiplied by scale",
            "Every component is multiplied by scale",
            -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->equal = ufo_filter_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoFilterTaskPrivate));
}

static void
ufo_filter_task_init (UfoFilterTask *self)
{
    UfoFilterTaskPrivate *priv;
    self->priv = priv = UFO_FILTER_TASK_GET_PRIVATE (self);
    priv->kernel = NULL;
    priv->filter_mem = NULL;
    priv->filter = FILTER_RAMP;
    priv->cutoff = 0.5f;
    priv->bw_order = 4.0f;
    priv->fb_tau = 0.1f;
    priv->fb_theta = 1.0f;
    priv->scale = 1.0f;
}
