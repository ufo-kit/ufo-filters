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
#include "ufo-sharpness-measure-task.h"

/**
 * SECTION:ufo-sharpness-measure-task
 * @Short_description: Measure sharpness of an image region
 * @Title: sharpness-measure
 *
 */

struct _UfoSharpnessMeasureTaskPrivate {
    gdouble sharpness;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoSharpnessMeasureTask, ufo_sharpness_measure_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_SHARPNESS_MEASURE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_SHARPNESS_MEASURE_TASK, UfoSharpnessMeasureTaskPrivate))

enum {
    PROP_0,
    PROP_SHARPNESS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_sharpness_measure_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_SHARPNESS_MEASURE_TASK, NULL));
}

static void
ufo_sharpness_measure_task_setup (UfoTask *task,
                              UfoResources *resources,
                              GError **error)
{
}

static void
ufo_sharpness_measure_task_get_requisition (UfoTask *task,
                                            UfoBuffer **inputs,
                                            UfoRequisition *requisition)
{
    requisition->n_dims = 0;
}

static void
ufo_sharpness_measure_task_get_structure (UfoTask *task,
                                          guint *n_inputs,
                                          UfoInputParam **in_params,
                                          UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_PROCESSOR;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
}

static gdouble
measure_sharpness (gfloat *data,
                   guint width,
                   guint height)
{
    gdouble sum = 0.0;

    for (guint y = 1; y < height; y++) {
        for (guint x = 1; x < width; x++) {
            guint index;
            gdouble h_gradient, v_gradient;
            
            index = y * width + x;
            h_gradient = fabs (data[index] - data[index-1]);
            v_gradient = fabs (data[index] - data[index-width]);
            sum += h_gradient + v_gradient;
        }
    }

    return sum / 2.0 / (width * height);
}

static gboolean
ufo_sharpness_measure_task_process (UfoCpuTask *task,
                                    UfoBuffer **inputs,
                                    UfoBuffer *output,
                                    UfoRequisition *requisition)
{
    UfoSharpnessMeasureTaskPrivate *priv;
    UfoRequisition req;
    gfloat *data;

    priv = UFO_SHARPNESS_MEASURE_TASK_GET_PRIVATE (task);
    data = ufo_buffer_get_host_array (inputs[0], NULL);
    ufo_buffer_get_requisition (inputs[0], &req);

    priv->sharpness = measure_sharpness (data, (guint) req.dims[0], (guint) req.dims[1]);
    g_object_notify (G_OBJECT (task), "sharpness");

    return TRUE;
}

static void
ufo_sharpness_measure_task_set_property (GObject *object,
                                         guint property_id,
                                         const GValue *value,
                                         GParamSpec *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_sharpness_measure_task_get_property (GObject *object,
                                         guint property_id,
                                         GValue *value,
                                         GParamSpec *pspec)
{
    UfoSharpnessMeasureTaskPrivate *priv = UFO_SHARPNESS_MEASURE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SHARPNESS:
            g_value_set_double (value, priv->sharpness);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_sharpness_measure_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_sharpness_measure_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_sharpness_measure_task_setup;
    iface->get_structure = ufo_sharpness_measure_task_get_structure;
    iface->get_requisition = ufo_sharpness_measure_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_sharpness_measure_task_process;
}

static void
ufo_sharpness_measure_task_class_init (UfoSharpnessMeasureTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_sharpness_measure_task_set_property;
    gobject_class->get_property = ufo_sharpness_measure_task_get_property;
    gobject_class->finalize = ufo_sharpness_measure_task_finalize;

    properties[PROP_SHARPNESS] =
        g_param_spec_double ("sharpness",
            "Sharpness of the image region",
            "Dimensionless measure describing the sharpness of the image",
            0.0, 1.0, 0.0,
            G_PARAM_READABLE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoSharpnessMeasureTaskPrivate));
}

static void
ufo_sharpness_measure_task_init(UfoSharpnessMeasureTask *self)
{
    self->priv = UFO_SHARPNESS_MEASURE_TASK_GET_PRIVATE(self);
}
