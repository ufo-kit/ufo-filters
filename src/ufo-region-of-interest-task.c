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

#include <string.h>
#include "ufo-region-of-interest-task.h"

/**
 * SECTION:ufo-region-of-interest-task
 * @Short_description: Cut out a region of interest
 * @Title: region-of-interest
 *
 * Cut out a region of interest from any two-dimensional input. If the ROI is
 * (partially) outside the input, only accessible data will be copied.
 */

struct _UfoRegionOfInterestTaskPrivate {
    guint x;
    guint y;
    guint width;
    guint height;
    gboolean foo;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRegionOfInterestTask, ufo_region_of_interest_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_REGION_OF_INTEREST_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_REGION_OF_INTEREST_TASK, UfoRegionOfInterestTaskPrivate))

enum {
    PROP_0,
    PROP_X,
    PROP_Y,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_region_of_interest_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_REGION_OF_INTEREST_TASK, NULL));
}

static void
ufo_region_of_interest_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_region_of_interest_task_get_requisition (UfoTask *task,
                                             UfoBuffer **inputs,
                                             UfoRequisition *requisition)
{
    UfoRegionOfInterestTaskPrivate *priv;

    priv = UFO_REGION_OF_INTEREST_TASK_GET_PRIVATE (task);
    requisition->n_dims = 2;
    requisition->dims[0] = priv->width;
    requisition->dims[1] = priv->height;
}

static void
ufo_region_of_interest_task_get_structure (UfoTask *task,
                                           guint *n_inputs,
                                           UfoInputParam **in_params,
                                           UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
}

static gboolean
ufo_region_of_interest_task_process (UfoCpuTask *task,
                                     UfoBuffer **inputs,
                                     UfoBuffer *output,
                                     UfoRequisition *requisition)
{
    UfoRegionOfInterestTaskPrivate *priv;
    UfoRequisition req;
    guint x1, y1, x2, y2;
    guint rd_width, rd_height;
    guint in_width, in_height;
    gfloat *in_data;
    gfloat *out_data;

    priv = UFO_REGION_OF_INTEREST_TASK_GET_PRIVATE (task);
    x1 = priv->x;
    y1 = priv->y;
    x2 = x1 + priv->width;
    y2 = y1 + priv->height;

    ufo_buffer_get_requisition (inputs[0], &req);

    in_width = (guint) req.dims[0];
    in_height = (guint) req.dims[1];

    /* Don't do anything if we are completely out of bounds */
    if (x1 > in_width || y1 > in_height) {
        g_warning ("%i > %i or %i > %i", x1, in_width, y1, in_height);
        return FALSE;
    }

    rd_width = x2 > in_width ? in_width - x1 : priv->width;
    rd_height = y2 > in_height ? in_height - y1 : priv->height;

    in_data = ufo_buffer_get_host_array (inputs[0], NULL);
    out_data = ufo_buffer_get_host_array (output, NULL);

    /*
     * Removing the for loop for "width aligned" regions gives a marginal
     * speed-up of ~4 per cent.
     */
    if (rd_width == req.dims[0]) {
        g_memmove (out_data,
                   in_data + y1 * in_width, 
                   rd_width * rd_height * sizeof (gfloat));
    }
    else {
        for (guint y = 0; y < rd_height; y++) {
            g_memmove (out_data + y*priv->width,
                       in_data + (y + y1)*in_width + x1, 
                       rd_width * sizeof(gfloat));
        }
    }

    return TRUE;
}

static void
ufo_region_of_interest_task_set_property (GObject *object,
                                          guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec)
{
    UfoRegionOfInterestTaskPrivate *priv = UFO_REGION_OF_INTEREST_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_X:
            priv->x = g_value_get_uint (value);
            break;
        case PROP_Y:
            priv->y = g_value_get_uint (value);
            break;
        case PROP_WIDTH:
            priv->width = g_value_get_uint (value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_region_of_interest_task_get_property (GObject *object,
                                          guint property_id,
                                          GValue *value,
                                          GParamSpec *pspec)
{
    UfoRegionOfInterestTaskPrivate *priv = UFO_REGION_OF_INTEREST_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_X:
            g_value_set_uint (value, priv->x);
            break;
        case PROP_Y:
            g_value_set_uint (value, priv->y);
            break;
        case PROP_WIDTH:
            g_value_set_uint (value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint (value, priv->height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_region_of_interest_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_region_of_interest_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_region_of_interest_task_setup;
    iface->get_structure = ufo_region_of_interest_task_get_structure;
    iface->get_requisition = ufo_region_of_interest_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_region_of_interest_task_process;
}

static void
ufo_region_of_interest_task_class_init (UfoRegionOfInterestTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_region_of_interest_task_set_property;
    gobject_class->get_property = ufo_region_of_interest_task_get_property;
    gobject_class->finalize = ufo_region_of_interest_task_finalize;

    properties[PROP_X] = 
        g_param_spec_uint("x",
            "Horizontal coordinate",
            "Horizontal coordinate from where to read input",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_Y] = 
        g_param_spec_uint("y",
            "Vertical coordinate",
            "Vertical coordinate from where to read input",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_WIDTH] = 
        g_param_spec_uint("width",
            "Width",
            "Width of the region of interest",
            1, G_MAXUINT, 256,
            G_PARAM_READWRITE);

    properties[PROP_HEIGHT] = 
        g_param_spec_uint("height",
            "Height",
            "Height of the region of interest",
            1, G_MAXUINT, 256,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoRegionOfInterestTaskPrivate));
}

static void
ufo_region_of_interest_task_init(UfoRegionOfInterestTask *self)
{
    self->priv = UFO_REGION_OF_INTEREST_TASK_GET_PRIVATE(self);
    self->priv->x = 0;
    self->priv->y = 0;
    self->priv->width = 256;
    self->priv->height = 256;
}
