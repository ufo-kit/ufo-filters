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
get_min_max (gfloat *data, UfoRequisition *requisition, gfloat *min, gfloat *max)
{
    gsize n_elements = requisition->dims[0] * requisition->dims[1];
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
write_float_data (TIFF *tiff, gfloat *data, UfoRequisition *requisition)
{
    TIFFSetField (tiff, TIFFTAG_BITSPERSAMPLE, 32);
    TIFFSetField (tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);

    for (guint y = 0; y < requisition->dims[1]; y++, data += requisition->dims[0])
        TIFFWriteScanline (tiff, data, y, 0);
}

static void
write_8bit_data (TIFF *tiff, gfloat *data, UfoRequisition *requisition)
{
    guint8 *scanline;
    guint width, height;
    gfloat max, min;
    gfloat range;
    
    get_min_max (data, requisition, &min, &max);
    range = max - min;
    width = requisition->dims[0];
    height = requisition->dims[1];

    TIFFSetField (tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField (tiff, TIFFTAG_BITSPERSAMPLE, 8);

    scanline = g_malloc (requisition->dims[0]);

    for (guint y = 0; y < height; y++, data += width) {
        for (guint i = 0; i < width; i++)
            scanline[i] = (guint8) ((data[i] - min) / range * 255);

        TIFFWriteScanline (tiff, scanline, y, 0);
    }

    g_free (scanline);
}

static void
write_16bit_data (TIFF *tiff, gfloat *data, UfoRequisition *requisition) 
{
    guint16 *scanline;
    guint width, height;
    gfloat max, min;
    gfloat range;
    
    get_min_max (data, requisition, &min, &max);
    range = max - min;
    width = requisition->dims[0];
    height = requisition->dims[1];

    TIFFSetField (tiff, TIFFTAG_BITSPERSAMPLE, 16);
    scanline = g_malloc (width * 2);

    for (guint y = 0; y < height; y++, data += width) {
        for (guint i = 0; i < width; i++)
            scanline[i] = (guint16) ((data[i] - min) / range * 65535);

        TIFFWriteScanline (tiff, scanline, y, 0);
    }

    g_free (scanline);
}

static void
ufo_tiff_writer_write (UfoWriter *writer,
                       gpointer data,
                       UfoRequisition *requisition,
                       UfoBufferDepth depth)
{
    UfoTiffWriterPrivate *priv;

    priv = UFO_TIFF_WRITER_GET_PRIVATE (writer);
    g_assert (priv->tiff != NULL);

    TIFFSetField (priv->tiff, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
    TIFFSetField (priv->tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (priv->tiff, TIFFTAG_IMAGEWIDTH, requisition->dims[0]);
    TIFFSetField (priv->tiff, TIFFTAG_IMAGELENGTH, requisition->dims[1]);
    TIFFSetField (priv->tiff, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize (priv->tiff, (guint32) - 1));

    /*
     * I seriously don't know if this is supposed to be supported by the format,
     * but it's the only we way can write the page number without knowing the
     * final number of pages in advance.
     */
    TIFFSetField (priv->tiff, TIFFTAG_PAGENUMBER, priv->page, priv->page);

    switch (depth) {
        case UFO_BUFFER_DEPTH_8U:
            write_8bit_data (priv->tiff, data, requisition);
            break;
        case UFO_BUFFER_DEPTH_16U:
        case UFO_BUFFER_DEPTH_16S:
            write_16bit_data (priv->tiff, data, requisition);
            break;
        case UFO_BUFFER_DEPTH_32F:
            write_float_data (priv->tiff, data, requisition);
            break;
        default:
            g_warning ("write:tiff: 32 bit signed and unsigned not supported");
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
