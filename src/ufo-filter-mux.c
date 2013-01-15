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

#include "ufo-filter-mux.h"

/**
 * SECTION:ufo-filter-mux
 * @Short_description: Multiplex two input streams
 * @Title: mux
 */

G_DEFINE_TYPE(UfoFilterMux, ufo_filter_mux, UFO_TYPE_FILTER)

#define UFO_FILTER_MUX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_MUX, UfoFilterMuxPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

static GError *ufo_filter_mux_process(UfoFilter *filter)
{
    UfoChannel *input_channels[2] = { NULL, NULL };
    input_channels[0] = ufo_filter_get_input_channel_by_name(filter, "input0");
    input_channels[1] = ufo_filter_get_input_channel_by_name(filter, "input1");
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);

    UfoBuffer *input1 = ufo_channel_get_input_buffer(input_channels[0]);
    UfoBuffer *input2 = ufo_channel_get_input_buffer(input_channels[1]);
    gint id1 = ufo_buffer_get_id(input1);
    gint id2 = ufo_buffer_get_id(input2);
    
    while ((input1 != NULL) || (input2 != NULL)) {
        while ((id1 < id2) && (input1 != NULL)) {
            ufo_channel_finalize_input_buffer(input_channels[0], input1);
            input1 = ufo_channel_get_input_buffer(input_channels[0]);
            id1 = ufo_buffer_get_id(input1);
        }
        
        while ((id2 < id1) && (input2 != NULL)) {
            ufo_channel_finalize_input_buffer(input_channels[0], input2);
            input2 = ufo_channel_get_input_buffer(input_channels[0]);
            id2 = ufo_buffer_get_id(input2);
        }

        if (input1 != NULL) {
            ufo_channel_finalize_input_buffer(input_channels[0], input1);
            input1 = ufo_channel_get_input_buffer(input_channels[0]);
            id1 = input1 == NULL ? -1 : ufo_buffer_get_id(input1);
        }
        
        if (input2 != NULL) {
            ufo_channel_finalize_input_buffer(input_channels[0], input2);
            input2 = ufo_channel_get_input_buffer(input_channels[0]);
            id2 = input2 == NULL ? -1 : ufo_buffer_get_id(input2);
        }
    }
    
    ufo_channel_finish(output_channel);
    return NULL;
}

static void ufo_filter_mux_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    /* Handle all properties accordingly */
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_mux_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    /* Handle all properties accordingly */
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_mux_class_init(UfoFilterMuxClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_mux_set_property;
    gobject_class->get_property = ufo_filter_mux_get_property;
    filter_class->process = ufo_filter_mux_process;
}

static void ufo_filter_mux_init(UfoFilterMux *self)
{
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_MUX, NULL);
}
