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

#include <math.h>
#include "ufo-flat-field-correction-task.h"

/**
 * SECTION:ufo-flat-field-correction-task
 * @Short_description: Flat field correct projections
 * @Title: flat_field_correction
 *
 * Reads three data streams: Projection data on input 0, (averaged) dark field
 * data on input 1 and (averaged) flat field data on input 2. The node outputs
 * the flat field correction of the input data. If
 * #UfoFlatFieldCorrectionTask:absorption-correction is %TRUE, the negative
 * logarithm is taken.
 */

struct _UfoFlatFieldCorrectionTaskPrivate {
    gboolean absorption_correction;
    gboolean fix_nan_and_inf;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFlatFieldCorrectionTask, ufo_flat_field_correction_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FLAT_FIELD_CORRECTION_TASK, UfoFlatFieldCorrectionTaskPrivate))

enum {
    PROP_0,
    PROP_ABSORPTION_CORRECTION,
    PROP_FIX_NAN_AND_INF,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_flat_field_correction_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FLAT_FIELD_CORRECTION_TASK, NULL));
}

static void
ufo_flat_field_correction_task_setup (UfoTask *task,
                                      UfoResources *resources,
                                      GError **error)
{
}

static void
ufo_flat_field_correction_task_get_requisition (UfoTask *task,
                                                UfoBuffer **inputs,
                                                UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static void
ufo_flat_field_correction_task_get_structure (UfoTask *task,
                                              guint *n_inputs,
                                              UfoInputParam **in_params,
                                              UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_PROCESSOR;
    *n_inputs = 3;
    *in_params = g_new0 (UfoInputParam, 3);
    (*in_params)[0].n_dims = 2;
    (*in_params)[1].n_dims = 2;
    (*in_params)[2].n_dims = 2;
}

static gboolean
ufo_flat_field_correction_task_process (UfoCpuTask *task,
                                        UfoBuffer **inputs,
                                        UfoBuffer *output,
                                        UfoRequisition *requisition)
{
    UfoFlatFieldCorrectionTaskPrivate *priv;
    UfoProfiler *profiler;
    gfloat *proj_data;
    gfloat *dark_data;
    gfloat *flat_data;
    gfloat *out_data;
    gsize n_pixels;

    priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (task);

    proj_data = ufo_buffer_get_host_array (inputs[0], NULL);
    dark_data = ufo_buffer_get_host_array (inputs[1], NULL);
    flat_data = ufo_buffer_get_host_array (inputs[2], NULL);
    out_data = ufo_buffer_get_host_array (output, NULL);
    n_pixels = requisition->dims[0] * requisition->dims[1];
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    ufo_profiler_start (profiler, UFO_PROFILER_TIMER_CPU);

    /* Flat field correction */
    for (gsize i = 0; i < n_pixels; i++)
        out_data[i] = (proj_data[i] - dark_data[i]) / (flat_data[i] - dark_data[i]);

    /* Optional absorption correction */
    if (priv->absorption_correction) {
        for (gsize i = 0; i < n_pixels; i++)
            out_data[i] = (gfloat) (- log (out_data[i]));
    }

    /* Fix NANs and INFs */
    if (priv->fix_nan_and_inf) {
        for (gsize i = 0; i < n_pixels; i++)
        if (isnan (out_data[i]) || isinf (out_data[i]))
            out_data[i] = 0.0;
    }

    ufo_profiler_stop (profiler, UFO_PROFILER_TIMER_CPU);

    return TRUE;
}

static void
ufo_flat_field_correction_task_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
    UfoFlatFieldCorrectionTaskPrivate *priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_ABSORPTION_CORRECTION:
            priv->absorption_correction = g_value_get_boolean (value);
            break;
        case PROP_FIX_NAN_AND_INF:
            priv->fix_nan_and_inf = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_flat_field_correction_task_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
    UfoFlatFieldCorrectionTaskPrivate *priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_ABSORPTION_CORRECTION:
            g_value_set_boolean (value, priv->absorption_correction);
            break;
        case PROP_FIX_NAN_AND_INF:
            g_value_set_boolean (value, priv->fix_nan_and_inf);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_flat_field_correction_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_flat_field_correction_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_flat_field_correction_task_setup;
    iface->get_structure = ufo_flat_field_correction_task_get_structure;
    iface->get_requisition = ufo_flat_field_correction_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_flat_field_correction_task_process;
}

static void
ufo_flat_field_correction_task_class_init (UfoFlatFieldCorrectionTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_flat_field_correction_task_set_property;
    gobject_class->get_property = ufo_flat_field_correction_task_get_property;
    gobject_class->finalize = ufo_flat_field_correction_task_finalize;

    properties[PROP_ABSORPTION_CORRECTION] =
        g_param_spec_boolean("absorption-correction",
                             "Take the negative natural logarithm of the result",
                             "Take the negative natural logarithm of the result",
                             FALSE,
                             G_PARAM_READWRITE);

    properties[PROP_FIX_NAN_AND_INF] =
        g_param_spec_boolean("fix-nan-and-inf",
                             "Replace NAN and INF values with 0.0",
                             "Replace NAN and INF values with 0.0",
                             FALSE,
                             G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoFlatFieldCorrectionTaskPrivate));
}

static void
ufo_flat_field_correction_task_init(UfoFlatFieldCorrectionTask *self)
{
    self->priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE(self);
    self->priv->absorption_correction = FALSE;
    self->priv->fix_nan_and_inf = FALSE;
}
