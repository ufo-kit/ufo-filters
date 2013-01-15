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

#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-subtract.h"

/**
 * SECTION:ufo-filter-subtract
 * @Short_description: A short description
 * @Title: A short title
 *
 * Some in-depth information
 */

struct _UfoFilterSubtractPrivate {
    guint n_pixels;
};

G_DEFINE_TYPE(UfoFilterSubtract, ufo_filter_subtract, UFO_TYPE_FILTER)

#define UFO_FILTER_SUBTRACT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_SUBTRACT, UfoFilterSubtractPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

static GParamSpec *subtract_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_subtract_initialize (UfoFilter *filter, UfoBuffer *input[], guint **dims, GError **error)
{
    UfoFilterSubtractPrivate *priv;
    guint width;
    guint height;

    priv = UFO_FILTER_SUBTRACT_GET_PRIVATE (filter);
    ufo_buffer_get_2d_dimensions (input[0], &width, &height);
    priv->n_pixels = width * height;

    dims[0][0] = width;
    dims[0][1] = height;
}

static void
ufo_filter_subtract_process_cpu (UfoFilter *filter, UfoBuffer *input[], UfoBuffer *output[], GError **error)
{
    UfoFilterSubtractPrivate *priv;
    cl_command_queue cmd_queue;
    gfloat *indata_a;
    gfloat *indata_b;
    gfloat *outdata;

    priv = UFO_FILTER_SUBTRACT_GET_PRIVATE (filter);
    cmd_queue = ufo_filter_get_command_queue (UFO_FILTER (filter));

    indata_a = ufo_buffer_get_host_array (input[0], cmd_queue);
    indata_b = ufo_buffer_get_host_array (input[1], cmd_queue);
    outdata = ufo_buffer_get_host_array (output[0], cmd_queue);

    for (guint i = 0; i < priv->n_pixels; i++)
        outdata[i] = indata_a[i] - indata_b[i];
}

static void
ufo_filter_subtract_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterSubtractPrivate *priv;

    priv = UFO_FILTER_SUBTRACT_GET_PRIVATE (object);

    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_subtract_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterSubtractPrivate *priv;

    priv = UFO_FILTER_SUBTRACT_GET_PRIVATE (object);

    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_subtract_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_filter_subtract_parent_class)->finalize (object);
}

static void
ufo_filter_subtract_class_init (UfoFilterSubtractClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS (klass);

    gobject_class->set_property = ufo_filter_subtract_set_property;
    gobject_class->get_property = ufo_filter_subtract_get_property;
    gobject_class->finalize = ufo_filter_subtract_finalize;
    filter_class->initialize = ufo_filter_subtract_initialize;
    filter_class->process_cpu = ufo_filter_subtract_process_cpu;

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, subtract_properties[i]);

    g_type_class_add_private (gobject_class, sizeof (UfoFilterSubtractPrivate));
}

static void
ufo_filter_subtract_init (UfoFilterSubtract *self)
{
    UfoInputParameter input_params[] = {
        { 2, UFO_FILTER_INFINITE_INPUT },
        { 2, UFO_FILTER_INFINITE_INPUT }
    };

    UfoOutputParameter output_params[] = {{2}};

    self->priv = UFO_FILTER_SUBTRACT_GET_PRIVATE (self);
    ufo_filter_register_inputs (UFO_FILTER (self), 2, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new (void)
{
    return g_object_new (UFO_TYPE_FILTER_SUBTRACT, NULL);
}
