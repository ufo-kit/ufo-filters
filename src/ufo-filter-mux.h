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

#ifndef __UFO_FILTER_MUX_H
#define __UFO_FILTER_MUX_H

#include <glib.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_MUX             (ufo_filter_mux_get_type())
#define UFO_FILTER_MUX(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_MUX, UfoFilterMux))
#define UFO_IS_FILTER_MUX(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_MUX))
#define UFO_FILTER_MUX_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_MUX, UfoFilterMuxClass))
#define UFO_IS_FILTER_MUX_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_MUX))
#define UFO_FILTER_MUX_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_MUX, UfoFilterMuxClass))

typedef struct _UfoFilterMux           UfoFilterMux;
typedef struct _UfoFilterMuxClass      UfoFilterMuxClass;
typedef struct _UfoFilterMuxPrivate    UfoFilterMuxPrivate;

struct _UfoFilterMux {
    /*< private >*/
    UfoFilter parent_instance;

    UfoFilterMuxPrivate *priv;
};

/**
 * UfoFilterMuxClass:
 *
 * #UfoFilterMux class
 */
struct _UfoFilterMuxClass {
    /*< private >*/
    UfoFilterClass parent_class;
};

GType ufo_filter_mux_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
