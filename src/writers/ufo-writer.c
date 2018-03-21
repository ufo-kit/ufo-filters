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

gboolean
ufo_writer_can_open (UfoWriter *writer,
                     const gchar *filename)
{
    return UFO_WRITER_GET_IFACE (writer)->can_open (writer, filename);
}

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
                  UfoWriterImage *image)
{
    ufo_writer_convert_inplace (image);
    UFO_WRITER_GET_IFACE (writer)->write (writer, image);
}

static void
get_min_max (UfoWriterImage *image, gfloat *src, gsize n_elements, gfloat *min, gfloat *max)
{
    if (image->max > -G_MAXFLOAT && image->min < G_MAXFLOAT) {
        *max = image->max;
        *min = image->min;
        return;
    }

    /* TODO: We should issue a warning if only one of max or min was set by the
     * user ... */

    /* gsize n_elements = get_num_elements (image->requisition); */
    gfloat cmax = -G_MAXFLOAT;
    gfloat cmin = G_MAXFLOAT;
    /* gfloat *src = (gfloat *) image->data; */

    for (gsize i = 0; i < n_elements; i++) {
        if (src[i] < cmin)
            cmin = src[i];

        if (src[i] > cmax)
            cmax = src[i];
    }

    *max = cmax;
    *min = cmin;
}

static void
convert_to_8bit (UfoWriterImage *image)
{
    gfloat *src;
    guint8 *dst;
    gfloat max, min, scale;
    guint n_planes;
    gsize plane_size;

    n_planes = image->requisition->n_dims == 3 ? image->requisition->dims[2] : 1;
    plane_size = image->requisition->dims[0] * image->requisition->dims[1];

    for (guint plane = 0; plane < n_planes; plane++) {
        src = ((gfloat *) image->data) + plane * plane_size;
        dst = (guint8 *) src;
        get_min_max (image, src, plane_size, &min, &max);
        scale = 255.0f / (max - min);

        /* first clip */
        if (min < max) {
            for (gsize i = 0; i < plane_size; i++) {
                if (src[i] < min)
                    src[i] = min;
                else if (src[i] > max)
                    src[i] = max;
            }
        }
        else {
            for (gsize i = 0; i < plane_size; i++) {
                if (src[i] < max)
                    src[i] = max;
                else if (src[i] > min)
                    src[i] = min;
            }
        }

        /* then rescale */
        for (gsize i = 0; i < plane_size; i++)
            dst[i] = (guint8) ((src[i] - min) * scale);
    }
}

static void
convert_to_16bit (UfoWriterImage *image)
{
    gfloat *src;
    guint16 *dst;
    gfloat max, min, scale;
    guint n_planes;
    gsize plane_size;

    n_planes = image->requisition->n_dims == 3 ? image->requisition->dims[2] : 1;
    plane_size = image->requisition->dims[0] * image->requisition->dims[1];

    for (guint plane = 0; plane < n_planes; plane++) {
        src = ((gfloat *) image->data) + plane * plane_size;
        dst = (guint16 *) src;
        get_min_max (image, src, plane_size, &min, &max);
        scale = 65535.0f / (max - min);

        if (min < max) {
            for (gsize i = 0; i < plane_size; i++) {
                if (src[i] < min)
                    src[i] = min;
                else if (src[i] > max)
                    src[i] = max;
            }
        }
        else {
            for (gsize i = 0; i < plane_size; i++) {
                if (src[i] < max)
                    src[i] = max;
                else if (src[i] > min)
                    src[i] = min;
            }
        }

        /* TODO: good opportunity for some SSE acceleration */
        for (gsize i = 0; i < plane_size; i++)
            dst[i] = (guint16) ((src[i] - min) * scale);
    }
}

void
ufo_writer_convert_inplace (UfoWriterImage *image)
{
    /*
     * Since we convert to data requiring less bytes per pixel than the native
     * float format, we can do everything in-place.
     */
    switch (image->depth) {
        case UFO_BUFFER_DEPTH_8U:
            convert_to_8bit (image);
            break;
        case UFO_BUFFER_DEPTH_16U:
        case UFO_BUFFER_DEPTH_16S:
            convert_to_16bit (image);
            break;
        default:
            break;
    }
}

static void
ufo_writer_default_init (UfoWriterInterface *iface)
{
}
