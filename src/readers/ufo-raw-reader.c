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

#include <stdio.h>
#include <stdlib.h>

#include "readers/ufo-reader.h"
#include "readers/ufo-raw-reader.h"


struct _UfoRawReaderPrivate {
    FILE *fp;
    gsize total_size;
    gsize frame_size;
    gsize bytes_per_pixel;
    guint width;
    guint height;
    gulong offset;
    UfoBufferDepth bitdepth;
};

static void ufo_reader_interface_init (UfoReaderIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRawReader, ufo_raw_reader, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_READER,
                                                ufo_reader_interface_init))

#define UFO_RAW_READER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_RAW_READER, UfoRawReaderPrivate))

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_BITDEPTH,
    PROP_OFFSET,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoRawReader *
ufo_raw_reader_new (void)
{
    UfoRawReader *reader = g_object_new (UFO_TYPE_RAW_READER, NULL);
    return reader;
}

static gboolean
ufo_raw_reader_can_open (UfoReader *reader,
                         const gchar *filename)
{
    UfoRawReaderPrivate *priv;

    priv = UFO_RAW_READER_GET_PRIVATE (reader);

    if (!g_str_has_suffix (filename, ".raw"))
        return FALSE;

    if (priv->width == 0 || priv->height == 0 || priv->bitdepth == UFO_BUFFER_DEPTH_INVALID) {
        g_warning ("`raw-width', `raw-height' or `raw-bitdepth' was not set");
        return FALSE;
    }

    return TRUE;
}

static void
ufo_raw_reader_open (UfoReader *reader,
                     const gchar *filename)
{
    UfoRawReaderPrivate *priv;

    priv = UFO_RAW_READER_GET_PRIVATE (reader);
    priv->fp = fopen (filename, "rb");

    fseek (priv->fp, 0L, SEEK_END);
    priv->total_size = (gsize) ftell (priv->fp);
    priv->frame_size = priv->width * priv->height * priv->bytes_per_pixel;
    fseek (priv->fp, 0L, SEEK_SET);
}

static void
ufo_raw_reader_close (UfoReader *reader)
{
    UfoRawReaderPrivate *priv;

    priv = UFO_RAW_READER_GET_PRIVATE (reader);
    g_assert (priv->fp != NULL);
    fclose (priv->fp);
    priv->fp = NULL;
    priv->total_size = 0;
}

static gboolean
ufo_raw_reader_data_available (UfoReader *reader)
{
    UfoRawReaderPrivate *priv;
    glong pos;

    priv = UFO_RAW_READER_GET_PRIVATE (reader);
    pos = ftell (priv->fp);
    return priv->fp != NULL && pos >= 0 && (((gulong) pos) + priv->offset + priv->frame_size) <= priv->total_size;
}

static void
ufo_raw_reader_read (UfoReader *reader,
                     UfoBuffer *buffer,
                     UfoRequisition *requisition,
                     guint roi_y,
                     guint roi_height,
                     guint roi_step)
{
    UfoRawReaderPrivate *priv;
    gchar *data;

    priv = UFO_RAW_READER_GET_PRIVATE (reader);
    data = (gchar *) ufo_buffer_get_host_array (buffer, NULL);

    fseek (priv->fp, priv->offset, SEEK_CUR);

    /* We never read more than we can store */
    if (fread (data, 1, priv->frame_size, priv->fp) != ((gsize) priv->frame_size))
        g_warning ("Could not read enough data");
}

static void
ufo_raw_reader_get_meta (UfoReader *reader,
                         gsize *width,
                         gsize *height,
                         UfoBufferDepth *bitdepth)
{
    UfoRawReaderPrivate *priv;

    priv = UFO_RAW_READER_GET_PRIVATE (reader);
    *width = (gsize) priv->width;
    *height = (gsize) priv->height;
    *bitdepth = priv->bitdepth;
}

static void
ufo_raw_reader_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
    UfoRawReaderPrivate *priv = UFO_RAW_READER_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_WIDTH:
            priv->width = g_value_get_uint (value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint (value);
            break;
        case PROP_BITDEPTH:
            switch (g_value_get_uint (value)) {
                case 8:
                    priv->bitdepth = UFO_BUFFER_DEPTH_8U;
                    priv->bytes_per_pixel = 1;
                    break;
                case 16:
                    priv->bitdepth = UFO_BUFFER_DEPTH_16U;
                    priv->bytes_per_pixel = 2;
                    break;
                case 32:
                    priv->bitdepth = UFO_BUFFER_DEPTH_32F;
                    priv->bytes_per_pixel = 4;
                    break;
                default:
                    g_warning ("Cannot set bitdepth other than 8, 16 or 32.");
            }
            break;
        case PROP_OFFSET:
            priv->offset = g_value_get_ulong (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_raw_reader_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
    UfoRawReaderPrivate *priv = UFO_RAW_READER_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_WIDTH:
            g_value_set_uint (value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint (value, priv->height);
            break;
        case PROP_BITDEPTH:
            g_value_set_uint (value, priv->bitdepth);
            break;
        case PROP_OFFSET:
            g_value_set_ulong (value, priv->offset);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_raw_reader_finalize (GObject *object)
{
    UfoRawReaderPrivate *priv;

    priv = UFO_RAW_READER_GET_PRIVATE (object);

    if (priv->fp != NULL) {
        fclose (priv->fp);
        priv->fp = NULL;
    }

    G_OBJECT_CLASS (ufo_raw_reader_parent_class)->finalize (object);
}

static void
ufo_reader_interface_init (UfoReaderIface *iface)
{
    iface->can_open = ufo_raw_reader_can_open;
    iface->open = ufo_raw_reader_open;
    iface->close = ufo_raw_reader_close;
    iface->read = ufo_raw_reader_read;
    iface->get_meta = ufo_raw_reader_get_meta;
    iface->data_available = ufo_raw_reader_data_available;
}

static void
ufo_raw_reader_class_init (UfoRawReaderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_raw_reader_set_property;
    gobject_class->get_property = ufo_raw_reader_get_property;
    gobject_class->finalize = ufo_raw_reader_finalize;

    properties[PROP_WIDTH] =
        g_param_spec_uint("width",
            "Width of raw image",
            "Width of raw image",
            0, G_MAXUINT, G_MAXUINT,
            G_PARAM_READWRITE);

    properties[PROP_HEIGHT] =
        g_param_spec_uint("height",
            "Height of raw image",
            "Height of raw image",
            0, G_MAXUINT, G_MAXUINT,
            G_PARAM_READWRITE);

    properties[PROP_BITDEPTH] =
        g_param_spec_uint("bitdepth",
            "Bitdepth of raw image",
            "Bitdepth of raw image",
            0, G_MAXUINT, G_MAXUINT,
            G_PARAM_READWRITE);

    properties[PROP_OFFSET] =
        g_param_spec_ulong("offset",
            "Offset to the beginning of image in bytes",
            "Offset to the beginning of image in bytes",
            0, G_MAXULONG, 0,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof (UfoRawReaderPrivate));
}

static void
ufo_raw_reader_init (UfoRawReader *self)
{
    UfoRawReaderPrivate *priv = NULL;

    self->priv = priv = UFO_RAW_READER_GET_PRIVATE (self);
    priv->fp = NULL;
    priv->width = 0;
    priv->height = 0;
    priv->bitdepth = UFO_BUFFER_DEPTH_INVALID;
    priv->offset = 0L;
}
