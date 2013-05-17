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
 * backprojection node.
 */

typedef void (*SetupFunc)(UfoFilterTaskPrivate *priv, gfloat *coefficients, guint width);

struct _UfoFilterTaskPrivate {
    cl_context context;
    cl_kernel kernel;
    cl_mem  filter_mem;
    gfloat  bw_cutoff;
    gfloat  bw_order;
    SetupFunc setup;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFilterTask, ufo_filter_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_FILTER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_TASK, UfoFilterTaskPrivate))

enum {
    PROP_0,
    PROP_FILTER,
    PROP_BW_CUTOFF,
    PROP_BW_ORDER,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_filter_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FILTER_TASK, NULL));
}

static gboolean
ufo_filter_task_process (UfoGpuTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoFilterTaskPrivate *priv;
    UfoGpuNode *node;
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

    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue,
                                                       priv->kernel,
                                                       2, NULL, requisition->dims, NULL,
                                                       0, NULL, NULL));

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
    priv->kernel = ufo_resources_get_kernel (resources,
                                             "filter.cl",
                                             "filter",
                                             error);

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));

}

static void
mirror_coefficients (gfloat *filter, guint width)
{
    for (guint k = width/2; k < width; k += 2) {
        filter[k] = filter[width - k];
        filter[k + 1] = filter[width - k + 1];
    }
}

static void
compute_ramp_coefficients (UfoFilterTaskPrivate *priv,
                           gfloat *filter,
                           guint width)
{
    const gfloat scale = 0.25f / ((gfloat) width);

    for (guint k = 1; k < width / 4; k++) {
        filter[2*k] = ((gfloat) k) * scale;
        filter[2*k + 1] = filter[2*k];
    }
}

static void
compute_butterworth_coefficients (UfoFilterTaskPrivate *priv,
                                  gfloat *filter,
                                  guint width)
{
    const gfloat scale = 0.25f / ((gfloat) width);
    const guint n_samples = width / 4;

    for (guint i = 0; i < n_samples; i++) {
        const gfloat u = ((gfloat) i) / ((gfloat) n_samples);
        filter[2*i] = ((gfloat) i) * scale;
        filter[2*i] /= (1.0f + (gfloat) pow (u / priv->bw_cutoff, 2.0f * priv->bw_order));
        filter[2*i+1] = filter[2*i];
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

        priv->setup (priv, coefficients, width);
        mirror_coefficients (coefficients, width);

        priv->filter_mem = clCreateBuffer (priv->context,
                                           CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                           requisition->dims[0] * sizeof(float),
                                           coefficients,
                                           &cl_err);
        UFO_RESOURCES_CHECK_CLERR (cl_err);
        g_free (coefficients);
    }
}

static void
ufo_filter_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               UfoInputParam **in_params,
                               UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_PROCESSOR;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
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
    iface->get_structure = ufo_filter_task_get_structure;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
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
            {
                const char *type = g_value_get_string (value);

                if (!g_strcmp0 (type, "ramp"))
                    priv->setup = &compute_ramp_coefficients;
                else if (!g_strcmp0 (type, "butterworth"))
                    priv->setup = &compute_butterworth_coefficients;
            }
            break;
        case PROP_BW_CUTOFF:
            priv->bw_cutoff = g_value_get_float (value);
            break;
        case PROP_BW_ORDER:
            priv->bw_order = g_value_get_float (value);
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
            if (priv->setup == &compute_ramp_coefficients)
                g_value_set_string (value, "ramp");
            else if (priv->setup == &compute_butterworth_coefficients)
                g_value_set_string (value, "butterworth");
            break;
        case PROP_BW_CUTOFF:
            g_value_set_float (value, priv->bw_cutoff);
            break;
        case PROP_BW_ORDER:
            g_value_set_float (value, priv->bw_order);
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
            "Type of filter (\"ramp\", \"butterworth\")",
            "Type of filter (\"ramp\", \"butterworth\")",
            "ramp",
            G_PARAM_READWRITE);

    properties[PROP_BW_CUTOFF] =
        g_param_spec_float ("cutoff",
            "Relative cutoff frequency",
            "Relative cutoff frequency of the Butterworth filter",
            0.0f, 1.0f, 0.5f,
            G_PARAM_READWRITE);

    properties[PROP_BW_ORDER] =
        g_param_spec_float ("order",
            "Order of the Butterworth filter",
            "Order of the Butterworth filter",
            2.0f, 32.0f, 4.0f,
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
    priv->setup = compute_ramp_coefficients;
    priv->bw_cutoff = 0.5f;
    priv->bw_order = 4.0f;
}
