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

#include <tiffio.h>

#include "readers/ufo-reader.h"
#include "readers/ufo-tiff-reader.h"


struct _UfoTiffReaderPrivate {
    TIFF    *tiff;
    gboolean more;
};

static void ufo_reader_interface_init (UfoReaderIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoTiffReader, ufo_tiff_reader, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_READER,
                                                ufo_reader_interface_init))

#define UFO_TIFF_READER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_TIFF_READER, UfoTiffReaderPrivate))

UfoTiffReader *
ufo_tiff_reader_new (void)
{
    UfoTiffReader *reader = g_object_new (UFO_TYPE_TIFF_READER, NULL);
    return reader;
}

static gboolean
ufo_tiff_reader_can_open (UfoReader *reader,
                         const gchar *filename)
{
    return g_str_has_suffix (filename, ".tiff") || g_str_has_suffix (filename, ".tif");
}

static void
ufo_tiff_reader_open (UfoReader *reader,
                      const gchar *filename)
{
    UfoTiffReaderPrivate *priv;
    
    priv = UFO_TIFF_READER_GET_PRIVATE (reader);
    priv->tiff = TIFFOpen (filename, "r");
    priv->more = TRUE;
}

static void
ufo_tiff_reader_close (UfoReader *reader)
{
    UfoTiffReaderPrivate *priv;
    
    priv = UFO_TIFF_READER_GET_PRIVATE (reader);
    g_assert (priv->tiff != NULL);
    TIFFClose (priv->tiff);
    priv->tiff = NULL;
}

static gboolean
ufo_tiff_reader_data_available (UfoReader *reader)
{
    UfoTiffReaderPrivate *priv;
    
    priv = UFO_TIFF_READER_GET_PRIVATE (reader);

    return priv->more && priv->tiff != NULL;
}

static void
ufo_tiff_reader_read (UfoReader *reader,
                      UfoBuffer *buffer,
                      UfoRequisition *requisition,
                      guint roi_y,
                      guint roi_height,
                      guint roi_step)
{
    UfoTiffReaderPrivate *priv;
    gchar *data;
    tsize_t result;
    gsize offset;
    guint16 bits;
    gsize step;

    priv = UFO_TIFF_READER_GET_PRIVATE (reader);

    TIFFGetField (priv->tiff, TIFFTAG_BITSPERSAMPLE, &bits);
    step = requisition->dims[0] * bits / 8;
    data = (gchar *) ufo_buffer_get_host_array (buffer, NULL);
    offset = 0;

    for (guint i = roi_y; i < roi_y + roi_height; i += roi_step) {
        result = TIFFReadScanline (priv->tiff, data + offset, i, 0);

        if (result == -1) {
            g_warning ("Cannot read scanline");
            return;
        }

        offset += step;
    }

    priv->more = TIFFReadDirectory (priv->tiff) == 1;
}

static void
ufo_tiff_reader_get_meta (UfoReader *reader,
                          gsize *width,
                          gsize *height,
                          UfoBufferDepth *bitdepth)
{
    UfoTiffReaderPrivate *priv;
    guint32 tiff_width;
    guint32 tiff_height;
    guint16 bits_per_sample;
    
    priv = UFO_TIFF_READER_GET_PRIVATE (reader);
    g_assert (priv->tiff != NULL);

    TIFFGetField (priv->tiff, TIFFTAG_IMAGEWIDTH, &tiff_width);
    TIFFGetField (priv->tiff, TIFFTAG_IMAGELENGTH, &tiff_height);
    TIFFGetField (priv->tiff, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);

    *width = (gsize) tiff_width;
    *height = (gsize) tiff_height;

    switch (bits_per_sample) {
        case 8: 
            *bitdepth = UFO_BUFFER_DEPTH_8U;
            break;
        case 16:
            *bitdepth = UFO_BUFFER_DEPTH_16U;
            break;
        default:
            *bitdepth = UFO_BUFFER_DEPTH_32F;
    }
}

static void
ufo_tiff_reader_finalize (GObject *object)
{
    UfoTiffReaderPrivate *priv;
    
    priv = UFO_TIFF_READER_GET_PRIVATE (object);

    if (priv->tiff != NULL)
        ufo_tiff_reader_close (UFO_READER (object));

    G_OBJECT_CLASS (ufo_tiff_reader_parent_class)->finalize (object);
}

static void
ufo_reader_interface_init (UfoReaderIface *iface)
{
    iface->can_open = ufo_tiff_reader_can_open;
    iface->open = ufo_tiff_reader_open;
    iface->close = ufo_tiff_reader_close;
    iface->read = ufo_tiff_reader_read;
    iface->get_meta = ufo_tiff_reader_get_meta;
    iface->data_available = ufo_tiff_reader_data_available;
}

static void
ufo_tiff_reader_class_init(UfoTiffReaderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = ufo_tiff_reader_finalize;

    g_type_class_add_private (gobject_class, sizeof (UfoTiffReaderPrivate));
}

static void
ufo_tiff_reader_init (UfoTiffReader *self)
{
    UfoTiffReaderPrivate *priv = NULL;

    self->priv = priv = UFO_TIFF_READER_GET_PRIVATE (self);
    priv->tiff = NULL;
    priv->more = FALSE;
}
