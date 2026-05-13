/*
 * Copyright (C) 2026 Karlsruhe Institute of Technology
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

#include <string.h>
#include <openjpeg.h>

#include "writers/ufo-writer.h"
#include "writers/ufo-jpeg2000-writer.h"


struct _UfoJpeg2000WriterPrivate {
    gchar *filename;
};

static void ufo_writer_interface_init (UfoWriterIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoJpeg2000Writer, ufo_jpeg2000_writer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_WRITER,
                                                ufo_writer_interface_init))

#define UFO_JPEG2000_WRITER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_JPEG2000_WRITER, UfoJpeg2000WriterPrivate))

UfoJpeg2000Writer *
ufo_jpeg2000_writer_new (void)
{
    return g_object_new (UFO_TYPE_JPEG2000_WRITER, NULL);
}

static gboolean
ufo_jpeg2000_writer_can_open (UfoWriter *writer,
                              const gchar *filename)
{
    return g_str_has_suffix (filename, ".jp2") ||
           g_str_has_suffix (filename, ".j2k") ||
           g_str_has_suffix (filename, ".j2c");
}

static void
ufo_jpeg2000_writer_open (UfoWriter *writer,
                          const gchar *filename)
{
    UfoJpeg2000WriterPrivate *priv;

    priv = UFO_JPEG2000_WRITER_GET_PRIVATE (writer);
    g_free (priv->filename);
    priv->filename = g_strdup (filename);
}

static void
ufo_jpeg2000_writer_close (UfoWriter *writer)
{
    UfoJpeg2000WriterPrivate *priv;

    priv = UFO_JPEG2000_WRITER_GET_PRIVATE (writer);
    g_free (priv->filename);
    priv->filename = NULL;
}

static gboolean
fill_image_components (opj_image_t *jp2_image,
                       UfoWriterImage *image,
                       guint num_components)
{
    gsize num_pixels;

    num_pixels = image->requisition->dims[0] * image->requisition->dims[1];

    if (image->depth == UFO_BUFFER_DEPTH_8U) {
        const guint8 *source = image->data;

        for (gsize pixel = 0; pixel < num_pixels; pixel++) {
            for (guint component = 0; component < num_components; component++)
                jp2_image->comps[component].data[pixel] = source[pixel * num_components + component];
        }

        return TRUE;
    }

    if (image->depth == UFO_BUFFER_DEPTH_16U) {
        const guint16 *source = image->data;

        for (gsize pixel = 0; pixel < num_pixels; pixel++) {
            for (guint component = 0; component < num_components; component++)
                jp2_image->comps[component].data[pixel] = source[pixel * num_components + component];
        }

        return TRUE;
    }

    return FALSE;
}

static void
ufo_jpeg2000_writer_write (UfoWriter *writer,
                           UfoWriterImage *image)
{
    UfoJpeg2000WriterPrivate *priv;
    opj_cparameters_t parameters;
    opj_image_cmptparm_t component_parameters[3];
    opj_image_t *jp2_image = NULL;
    opj_codec_t *codec = NULL;
    opj_stream_t *stream = NULL;
    OPJ_CODEC_FORMAT codec_format;
    OPJ_COLOR_SPACE color_space;
    guint num_components;
    guint precision;
    gboolean is_rgb;
    gboolean success = FALSE;

    priv = UFO_JPEG2000_WRITER_GET_PRIVATE (writer);
    g_return_if_fail (priv->filename != NULL);

    is_rgb = image->requisition->n_dims == 3 && image->requisition->dims[2] == 3;
    num_components = is_rgb ? 3 : 1;

    if (image->depth != UFO_BUFFER_DEPTH_8U &&
        image->depth != UFO_BUFFER_DEPTH_16U) {
        image->depth = UFO_BUFFER_DEPTH_16U;
        ufo_writer_convert_inplace (image);
    }

    precision = image->depth == UFO_BUFFER_DEPTH_8U ? 8 : 16;
    memset (component_parameters, 0, sizeof (component_parameters));

    for (guint i = 0; i < num_components; i++) {
        component_parameters[i].dx = 1;
        component_parameters[i].dy = 1;
        component_parameters[i].w = image->requisition->dims[0];
        component_parameters[i].h = image->requisition->dims[1];
        component_parameters[i].prec = precision;
        component_parameters[i].sgnd = 0;
    }

    color_space = is_rgb ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY;
    jp2_image = opj_image_create (num_components, component_parameters, color_space);

    if (jp2_image == NULL)
        goto cleanup;

    jp2_image->x0 = 0;
    jp2_image->y0 = 0;
    jp2_image->x1 = image->requisition->dims[0];
    jp2_image->y1 = image->requisition->dims[1];

    if (!fill_image_components (jp2_image, image, num_components))
        goto cleanup;

    opj_set_default_encoder_parameters (&parameters);
    codec_format = g_str_has_suffix (priv->filename, ".jp2") ? OPJ_CODEC_JP2 : OPJ_CODEC_J2K;
    parameters.cod_format = codec_format == OPJ_CODEC_JP2 ? 1 : 0;

    codec = opj_create_compress (codec_format);
    if (codec == NULL)
        goto cleanup;

    if (!opj_setup_encoder (codec, &parameters, jp2_image))
        goto cleanup;

    stream = opj_stream_create_default_file_stream (priv->filename, OPJ_FALSE);
    if (stream == NULL)
        goto cleanup;

    success = opj_start_compress (codec, jp2_image, stream) &&
              opj_encode (codec, stream) &&
              opj_end_compress (codec, stream);

cleanup:
    if (!success)
        g_warning ("Could not write JPEG 2000 image `%s'.", priv->filename);

    if (stream != NULL)
        opj_stream_destroy (stream);

    if (codec != NULL)
        opj_destroy_codec (codec);

    if (jp2_image != NULL)
        opj_image_destroy (jp2_image);
}

static void
ufo_jpeg2000_writer_finalize (GObject *object)
{
    UfoJpeg2000WriterPrivate *priv;

    priv = UFO_JPEG2000_WRITER_GET_PRIVATE (object);
    g_free (priv->filename);
    priv->filename = NULL;

    G_OBJECT_CLASS (ufo_jpeg2000_writer_parent_class)->finalize (object);
}

static void
ufo_writer_interface_init (UfoWriterIface *iface)
{
    iface->can_open = ufo_jpeg2000_writer_can_open;
    iface->open = ufo_jpeg2000_writer_open;
    iface->close = ufo_jpeg2000_writer_close;
    iface->write = ufo_jpeg2000_writer_write;
}

static void
ufo_jpeg2000_writer_class_init (UfoJpeg2000WriterClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = ufo_jpeg2000_writer_finalize;

    g_type_class_add_private (gobject_class, sizeof (UfoJpeg2000WriterPrivate));
}

static void
ufo_jpeg2000_writer_init (UfoJpeg2000Writer *self)
{
    UfoJpeg2000WriterPrivate *priv = NULL;

    self->priv = priv = UFO_JPEG2000_WRITER_GET_PRIVATE (self);
    priv->filename = NULL;
}
