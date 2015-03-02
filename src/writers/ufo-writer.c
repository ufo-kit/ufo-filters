/*
 * Copyright (C) 2011-2015 Karlsruhe Institute of Technology
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

#include "ufo-writer.h"

typedef UfoWriterIface UfoWriterInterface;

G_DEFINE_INTERFACE (UfoWriter, ufo_writer, 0)


void
ufo_writer_open (UfoWriter *writer,
                 const gchar *filename)
{
    UFO_WRITER_GET_IFACE (writer)->open (writer, filename);
}

void
ufo_writer_close (UfoWriter *writer)
{
    UFO_WRITER_GET_IFACE (writer)->close (writer);
}

void
ufo_writer_write (UfoWriter *writer,
                  gpointer data,
                  UfoRequisition *requisition,
                  UfoBufferDepth depth)
{
    UFO_WRITER_GET_IFACE (writer)->write (writer, data, requisition, depth);
}

static gsize
get_num_elements (UfoRequisition *requisition)
{
    gsize count = 1;

    for (guint i = 0; i < requisition->n_dims; i++)
        count *= requisition->dims[i];

    return count;
}

static void
get_min_max (gfloat *data, UfoRequisition *requisition, gfloat *min, gfloat *max)
{
    gsize n_elements = get_num_elements (requisition);
    gfloat cmax = -G_MAXFLOAT;
    gfloat cmin = G_MAXFLOAT;

    for (gsize i = 0; i < n_elements; i++) {
        if (data[i] < cmin)
            cmin = data[i];

        if (data[i] > cmax)
            cmax = data[i];
    }

    *max = cmax;
    *min = cmin;
}

static void
convert_to_8bit (gfloat *src, UfoRequisition *requisition)
{
    guint8 *dst;
    gfloat max, min;
    gsize n_elements;

    get_min_max (src, requisition, &min, &max);
    n_elements = get_num_elements (requisition);
    dst = (guint8 *) src;

    if (min >= 0.0 && max <= 255) {
        for (gsize i = 0; i < n_elements; i++)
            dst[i] = (guint16) src[i];
    }
    else {
        gfloat range = max - min;

        for (gsize i = 0; i < n_elements; i++)
            dst[i] = (guint16) ((src[i] - min) / range * 255);
    }
}

static void
convert_to_16bit (gfloat *src, UfoRequisition *requisition)
{
    guint16 *dst;
    gfloat max, min;
    gsize n_elements;

    get_min_max (src, requisition, &min, &max);
    n_elements = get_num_elements (requisition);
    dst = (guint16 *) src;

    /* TODO: good opportunity for some SSE acceleration */
    if (min >= 0.0 && max <= 65535) {
        for (gsize i = 0; i < n_elements; i++)
            dst[i] = (guint16) src[i];
    }
    else {
        gfloat range = max - min;

        for (gsize i = 0; i < n_elements; i++)
            dst[i] = (guint16) ((src[i] - min) / range * 65535);
    }
}

void
ufo_writer_convert_inplace (gpointer data,
                            UfoRequisition *requisition,
                            UfoBufferDepth depth)
{
    /*
     * Since we convert to data requiring less bytes per pixel than the native
     * float format, we can do everything in-place.
     */
    switch (depth) {
        case UFO_BUFFER_DEPTH_8U:
            convert_to_8bit (data, requisition);
            break;
        case UFO_BUFFER_DEPTH_16U:
        case UFO_BUFFER_DEPTH_16S:
            convert_to_16bit (data, requisition);
            break;
        default:
            break;
    }
}

static void
ufo_writer_default_init (UfoWriterInterface *iface)
{
}
