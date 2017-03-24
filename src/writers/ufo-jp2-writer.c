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

#include <stdio.h>
#include <openjpeg.h>

#include "writers/ufo-writer.h"
#include "writers/ufo-jp2-writer.h"


struct _UfoJp2WriterPrivate {
    opj_stream_t *stream;
};

static void ufo_writer_interface_init (UfoWriterIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoJp2Writer, ufo_jp2_writer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_WRITER,
                                                ufo_writer_interface_init))

#define UFO_JP2_WRITER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_JP2_WRITER, UfoJp2WriterPrivate))

UfoJp2Writer *
ufo_jp2_writer_new (void)
{
    UfoJp2Writer *writer = g_object_new (UFO_TYPE_JP2_WRITER, NULL);
    return writer;
}

void
ufo_jp2_writer_set_quality (UfoJp2Writer *writer, gint quality)
{
}

static gboolean
ufo_jp2_writer_can_open (UfoWriter *writer,
                         const gchar *filename)
{
    return g_str_has_suffix (filename, ".jp2");
}

static void
ufo_jp2_writer_open (UfoWriter *writer,
                     const gchar *filename)
{
    UfoJp2WriterPrivate *priv;
    
    priv = UFO_JP2_WRITER_GET_PRIVATE (writer);
    priv->stream = opj_stream_create_default_file_stream (filename, OPJ_FALSE);
}

static void
ufo_jp2_writer_close (UfoWriter *writer)
{
    UfoJp2WriterPrivate *priv;
    
    priv = UFO_JP2_WRITER_GET_PRIVATE (writer);
    g_assert (priv->stream != NULL);
    opj_stream_destroy (priv->stream);
}

static void
ufo_jp2_writer_write (UfoWriter *writer,
                      UfoWriterImage *image)
{
    UfoJp2WriterPrivate *priv;
    opj_image_cmptparm_t component;
    opj_image_t *opj_image;

    priv = UFO_JP2_WRITER_GET_PRIVATE (writer);

    image->depth = UFO_BUFFER_DEPTH_16U;
    ufo_writer_convert_inplace (image);

    component.sgnd = 0;
    component.dx = 1;
    component.dy = 1;
    component.w = image->requisition->dims[0];
    component.h = image->requisition->dims[1];
    component.x0 = 0;
    component.y0 = 0;
    component.bpp = 16;
    component.prec = 16;

    opj_image = opj_image_create (1, &component, CLRSPC_GRAY);

    g_print ("image: %p\n", opj_image);

    opj_image_destroy (opj_image);

#if 0
    opj_cparameters_t parameters = {
        .cod_format = JP2_CFMT,
        .
#endif
}

static void
ufo_jp2_writer_finalize (GObject *object)
{
    UfoJp2WriterPrivate *priv;
    
    priv = UFO_JP2_WRITER_GET_PRIVATE (object);

    if (priv->fp != NULL)
        ufo_jp2_writer_close (UFO_WRITER (object));

    G_OBJECT_CLASS (ufo_jp2_writer_parent_class)->finalize (object);
}

static void
ufo_writer_interface_init (UfoWriterIface *iface)
{
    iface->can_open = ufo_jp2_writer_can_open;
    iface->open = ufo_jp2_writer_open;
    iface->close = ufo_jp2_writer_close;
    iface->write = ufo_jp2_writer_write;
}

static void
ufo_jp2_writer_class_init(UfoJp2WriterClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = ufo_jp2_writer_finalize;

    g_type_class_add_private (gobject_class, sizeof (UfoJp2WriterPrivate));
}

static void
ufo_jp2_writer_init (UfoJp2Writer *self)
{
    UfoJp2WriterPrivate *priv = NULL;

    self->priv = priv = UFO_JP2_WRITER_GET_PRIVATE (self);
    priv->fp = NULL;
}
