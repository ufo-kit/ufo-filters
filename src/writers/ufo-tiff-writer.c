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
#include <string.h>

#include "writers/ufo-writer.h"
#include "writers/ufo-tiff-writer.h"


struct _UfoTiffWriterPrivate {
    TIFF *tiff;
    guint page;
};

static void ufo_writer_interface_init (UfoWriterIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoTiffWriter, ufo_tiff_writer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_WRITER,
                                                ufo_writer_interface_init))

#define UFO_TIFF_WRITER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_TIFF_WRITER, UfoTiffWriterPrivate))

UfoTiffWriter *
ufo_tiff_writer_new (void)
{
    UfoTiffWriter *writer = g_object_new (UFO_TYPE_TIFF_WRITER, NULL);
    return writer;
}

static gboolean
ufo_tiff_writer_can_open (UfoWriter *writer,
                          const gchar *filename)
{
    return g_str_has_suffix (filename, ".tif") || g_str_has_suffix (filename, ".tiff");
}

static void
ufo_tiff_writer_open (UfoWriter *writer,
                      const gchar *filename)
{
    UfoTiffWriterPrivate *priv;
    
    priv = UFO_TIFF_WRITER_GET_PRIVATE (writer);
    priv->tiff = TIFFOpen (filename, "w");
    priv->page = 0;
}

static void
ufo_tiff_writer_close (UfoWriter *writer)
{
    UfoTiffWriterPrivate *priv;
    
    priv = UFO_TIFF_WRITER_GET_PRIVATE (writer);
    g_assert (priv->tiff != NULL);
    TIFFClose (priv->tiff);
    priv->tiff = NULL;
}

static void
ufo_tiff_writer_write (UfoWriter *writer,
                       UfoWriterImage *image)
{
    UfoTiffWriterPrivate *priv;
    guint bits_per_sample;
    gsize stride;
    gchar *buff;
    gboolean is_rgb;

    priv = UFO_TIFF_WRITER_GET_PRIVATE (writer);
    g_assert (priv->tiff != NULL);

    is_rgb = image->requisition->n_dims == 3 && image->requisition->dims[2] == 3;

    TIFFSetField (priv->tiff, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
    TIFFSetField (priv->tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (priv->tiff, TIFFTAG_IMAGEWIDTH, image->requisition->dims[0]);
    TIFFSetField (priv->tiff, TIFFTAG_IMAGELENGTH, image->requisition->dims[1]);
    TIFFSetField (priv->tiff, TIFFTAG_SAMPLESPERPIXEL, is_rgb ? 3 : 1);
    TIFFSetField (priv->tiff, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize (priv->tiff, (guint32) - 1));

    /*
     * I seriously don't know if this is supposed to be supported by the format,
     * but it's the only we way can write the page number without knowing the
     * final number of pages in advance.
     */
    TIFFSetField (priv->tiff, TIFFTAG_PAGENUMBER, priv->page, priv->page);

    switch (image->depth) {
        case UFO_BUFFER_DEPTH_8U:
            TIFFSetField (priv->tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
            bits_per_sample = 8;
            break;
        case UFO_BUFFER_DEPTH_16U:
        case UFO_BUFFER_DEPTH_16S:
            TIFFSetField (priv->tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
            bits_per_sample = 16;
            break;
        default:
            TIFFSetField (priv->tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
            bits_per_sample = 32;
    }

    TIFFSetField (priv->tiff, TIFFTAG_BITSPERSAMPLE, bits_per_sample);

    if (is_rgb) {
        gchar *scanline;
        gsize size;
        gsize offset;

        size = bits_per_sample / 8;
        stride = image->requisition->dims[0] * size;
        scanline = g_malloc (stride * 3);
        offset = image->requisition->dims[0] * image->requisition->dims[1] * sizeof (gfloat);

        for (guint y = 0; y < image->requisition->dims[1]; y++) {
            guint xs = 0;
            guint xd = 0;

            /* TODO: copy more efficient */
            for (; xs < image->requisition->dims[0]; xs += 1, xd += 3) {
                memcpy (scanline + xd * size, image->data + y * stride + xs * size, size);
                memcpy (scanline + xd * size + 1 * size, image->data + offset + y * stride + xs * size, size);
                memcpy (scanline + xd * size + 2 * size, image->data + 2 * offset + y * stride + xs * size, size);
            }

            TIFFWriteScanline (priv->tiff, scanline, y, 0);
        }
    }
    else {
        stride = image->requisition->dims[0] * bits_per_sample / 8;
        buff = (gchar *) image->data;

        for (guint y = 0; y < image->requisition->dims[1]; y++) {
            TIFFWriteScanline (priv->tiff, buff, y, 0);
            buff += stride;
        }
    }

    TIFFWriteDirectory (priv->tiff);
    priv->page++;
}

static void
ufo_tiff_writer_finalize (GObject *object)
{
    UfoTiffWriterPrivate *priv;
    
    priv = UFO_TIFF_WRITER_GET_PRIVATE (object);

    if (priv->tiff != NULL)
        ufo_tiff_writer_close (UFO_WRITER (object));

    G_OBJECT_CLASS (ufo_tiff_writer_parent_class)->finalize (object);
}

static void
ufo_writer_interface_init (UfoWriterIface *iface)
{
    iface->can_open = ufo_tiff_writer_can_open;
    iface->open = ufo_tiff_writer_open;
    iface->close = ufo_tiff_writer_close;
    iface->write = ufo_tiff_writer_write;
}

static void
ufo_tiff_writer_class_init(UfoTiffWriterClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = ufo_tiff_writer_finalize;

    g_type_class_add_private (gobject_class, sizeof (UfoTiffWriterPrivate));
}

static void
ufo_tiff_writer_init (UfoTiffWriter *self)
{
    UfoTiffWriterPrivate *priv = NULL;

    self->priv = priv = UFO_TIFF_WRITER_GET_PRIVATE (self);
    priv->tiff = NULL;
}
