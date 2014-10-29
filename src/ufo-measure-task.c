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

#include <gsl/gsl_statistics_float.h>
#include "ufo-measure-task.h"

/**
 * SECTION:ufo-measure-task
 * @Short_description: Measure basic image properties with GSL
 * @Title: measure
 *
 */

typedef enum {
    M_0,
    M_STD,
    M_MIN,
    M_MAX,
    M_LAST
} Metric;

static const gchar *metrics[] = {"std", "min", "max"};

struct _UfoMeasureTaskPrivate {
    Metric metric;
    gint axis;
};

enum {
    RESULT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };



static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoMeasureTask, ufo_measure_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_MEASURE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_MEASURE_TASK, UfoMeasureTaskPrivate))

enum {
    PROP_0,
    PROP_METRIC,
    PROP_AXIS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

static Metric
string_to_metric (const gchar *s)
{
    for (Metric i = M_0 + 1; i < M_LAST; i++) {
        if (!g_strcmp0 (s, metrics[i - 1]))
            return i;
    }

    return M_0;
}

UfoNode *
ufo_measure_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_MEASURE_TASK, NULL));
}

static void
ufo_measure_task_setup (UfoTask *task,
                        UfoResources *resources,
                        GError **error)
{
}

static void
ufo_measure_task_get_requisition (UfoTask *task,
                                  UfoBuffer **inputs,
                                  UfoRequisition *requisition)
{
    requisition->n_dims = 0;
}

static guint
ufo_measure_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_measure_task_get_num_dimensions (UfoTask *task,
                                     guint input)
{
    return 2;
}

static UfoTaskMode
ufo_measure_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_measure_task_process (UfoTask *task,
                          UfoBuffer **inputs,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoMeasureTaskPrivate *priv;
    UfoRequisition in_req;
    UfoRequisition result_req;
    UfoBuffer *result_buffer;
    gfloat *data;
    gfloat *result;
    
    priv = UFO_MEASURE_TASK_GET_PRIVATE (task);

    ufo_buffer_get_requisition (inputs[0], &in_req);

    result_req.n_dims = in_req.n_dims - 1;
    result_req.dims[0] = priv->axis < 0 ? 1 : in_req.dims[priv->axis];

    result_buffer = ufo_buffer_new (&result_req, NULL);
    result = ufo_buffer_get_host_array (result_buffer, NULL);
    data = ufo_buffer_get_host_array (inputs[0], NULL);

    if (priv->axis < 0) {
        guint n = ufo_buffer_get_size (inputs[0]) / sizeof (gfloat);

        switch (priv->metric) {
            case M_STD:
                result[0] = gsl_stats_float_sd (data, 1, n);
                break;

            case M_MIN:
                result[0] = gsl_stats_float_min (data, 1, n);
                break;

            case M_MAX:
                result[0] = gsl_stats_float_max (data, 1, n);
                break;

            default:
                break;
        }
    }

    g_signal_emit (task, signals[RESULT], 0, result_buffer);
    g_object_unref (result_buffer);

    return TRUE;
}

static void
ufo_measure_task_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
    UfoMeasureTaskPrivate *priv;
    
    priv = UFO_MEASURE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_AXIS:
            priv->axis = g_value_get_int (value);
            break;

        case PROP_METRIC:
            {
                Metric metric = string_to_metric (g_value_get_string (value));

                if (metric != M_0)
                    priv->metric = metric;
            }
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_measure_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoMeasureTaskPrivate *priv = UFO_MEASURE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_AXIS:
            g_value_set_int (value, priv->axis);
            break;
        case PROP_METRIC:
            g_value_set_string (value, metrics[priv->metric]);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_measure_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_measure_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_measure_task_setup;
    iface->get_num_inputs = ufo_measure_task_get_num_inputs;
    iface->get_num_dimensions = ufo_measure_task_get_num_dimensions;
    iface->get_mode = ufo_measure_task_get_mode;
    iface->get_requisition = ufo_measure_task_get_requisition;
    iface->process = ufo_measure_task_process;
}

static void
ufo_measure_task_class_init (UfoMeasureTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_measure_task_set_property;
    oclass->get_property = ufo_measure_task_get_property;
    oclass->finalize = ufo_measure_task_finalize;

    properties[PROP_METRIC] =
        g_param_spec_string ("metric",
            "Metric (std, min, max)",
            "Metric (std, min, max)",
            "",
            G_PARAM_READWRITE);

    properties[PROP_AXIS] =
        g_param_spec_int ("axis",
            "Along which axis to measure (-1, all)",
            "Along which axis to measure (-1, all)",
            -1, UFO_BUFFER_MAX_NDIMS, -1,
            G_PARAM_READWRITE);

    signals[RESULT] =
        g_signal_new ("result",
                      G_OBJECT_CLASS_TYPE (oclass),
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                      0,
                      NULL, NULL, g_cclosure_marshal_VOID__BOXED,
                      G_TYPE_NONE, 1, UFO_TYPE_BUFFER);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoMeasureTaskPrivate));
}

static void
ufo_measure_task_init(UfoMeasureTask *self)
{
    self->priv = UFO_MEASURE_TASK_GET_PRIVATE(self);
    self->priv->axis = -1;
    self->priv->metric = M_STD;
}
