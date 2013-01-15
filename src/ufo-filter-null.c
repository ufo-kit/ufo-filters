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

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-null.h"

/**
 * SECTION:ufo-filter-null
 * @Short_description: Discard input
 * @Title: null
 *
 * This node discards any input similar to what /dev/null provides.
 */

G_DEFINE_TYPE (UfoFilterNull, ufo_filter_null, UFO_TYPE_FILTER_SINK)

#define UFO_FILTER_NULL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_NULL, UfoFilterNullPrivate))

static void
ufo_filter_null_consume(UfoFilterSink *filter, UfoBuffer *params[], GError **error)
{
    /* We don't do anything here */
}

static void 
ufo_filter_null_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void 
ufo_filter_null_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void 
ufo_filter_null_class_init(UfoFilterNullClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterSinkClass *filter_class = UFO_FILTER_SINK_CLASS(klass);

    gobject_class->set_property = ufo_filter_null_set_property;
    gobject_class->get_property = ufo_filter_null_get_property;
    filter_class->consume = ufo_filter_null_consume;
}

static void 
ufo_filter_null_init(UfoFilterNull *self)
{
    UfoInputParameter input_params[] = {{2, UFO_FILTER_INFINITE_INPUT}};

    ufo_filter_register_inputs (UFO_FILTER (self), 1, input_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_NULL, NULL);
}

