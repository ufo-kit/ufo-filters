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

#include "readers/ufo-reader.h"
#include "readers/ufo-jpeg2000-reader.h"


struct _UfoJpeg2000ReaderPrivate {
    gchar *filename;
    opj_codec_t *codec;
    opj_stream_t *stream;
    opj_image_t *image;
    UfoBufferDepth bitdepth;
    gboolean more;
};

static void ufo_reader_interface_init (UfoReaderIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoJpeg2000Reader, ufo_jpeg2000_reader, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_READER,
                                                ufo_reader_interface_init))

#define UFO_JPEG2000_READER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_JPEG2000_READER, UfoJpeg2000ReaderPrivate))

UfoJpeg2000Reader *
ufo_jpeg2000_reader_new (void)
{
    return g_object_new (UFO_TYPE_JPEG2000_READER, NULL);
}

static gboolean
ufo_jpeg2000_reader_can_open (UfoReader *reader,
                              const gchar *filename)
{
    return g_str_has_suffix (filename, ".jp2") ||
           g_str_has_suffix (filename, ".j2k") ||
           g_str_has_suffix (filename, ".j2c");
}

static OPJ_CODEC_FORMAT
get_codec_format (const gchar *filename)
{
    if (g_str_has_suffix (filename, ".jp2"))
        return OPJ_CODEC_JP2;

    return OPJ_CODEC_J2K;
}

static void
cleanup_decoder (UfoJpeg2000ReaderPrivate *priv)
{
    if (priv->image != NULL) {
        opj_image_destroy (priv->image);
        priv->image = NULL;
    }

    if (priv->codec != NULL) {
        opj_destroy_codec (priv->codec);
        priv->codec = NULL;
    }

    if (priv->stream != NULL) {
        opj_stream_destroy (priv->stream);
        priv->stream = NULL;
    }
}

static gboolean
ufo_jpeg2000_reader_open (UfoReader *reader,
                          const gchar *filename,
                          guint start,
                          GError **error)
{
    UfoJpeg2000ReaderPrivate *priv;
    opj_dparameters_t parameters;
    guint threads;

    priv = UFO_JPEG2000_READER_GET_PRIVATE (reader);
    cleanup_decoder (priv);
    g_free (priv->filename);
    priv->filename = g_strdup (filename);
    priv->more = start == 0;

    priv->stream = opj_stream_create_default_file_stream (filename, OPJ_TRUE);

    if (priv->stream == NULL) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "Cannot open %s", filename);
        return FALSE;
    }

    priv->codec = opj_create_decompress (get_codec_format (filename));

    if (priv->codec == NULL) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "Cannot create JPEG 2000 decoder for %s", filename);
        return FALSE;
    }

    opj_set_default_decoder_parameters (&parameters);

    if (!opj_setup_decoder (priv->codec, &parameters)) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "Cannot set up JPEG 2000 decoder for %s", filename);
        return FALSE;
    }

    threads = g_get_num_processors ();

    if (threads > 1 && !opj_codec_set_threads (priv->codec, (int) threads))
        g_warning ("Could not enable %u OpenJPEG worker threads.", threads);

    if (!opj_read_header (priv->stream, priv->codec, &priv->image)) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "Cannot read JPEG 2000 header from %s", filename);
        return FALSE;
    }

    return TRUE;
}

static void
ufo_jpeg2000_reader_close (UfoReader *reader)
{
    UfoJpeg2000ReaderPrivate *priv;

    priv = UFO_JPEG2000_READER_GET_PRIVATE (reader);
    cleanup_decoder (priv);
    g_free (priv->filename);
    priv->filename = NULL;
    priv->more = FALSE;
}

static gboolean
ufo_jpeg2000_reader_data_available (UfoReader *reader)
{
    UfoJpeg2000ReaderPrivate *priv;

    priv = UFO_JPEG2000_READER_GET_PRIVATE (reader);
    return priv->more && priv->image != NULL;
}

static gboolean
components_are_compatible (opj_image_t *image)
{
    for (guint i = 1; i < image->numcomps; i++) {
        if (image->comps[i].w != image->comps[0].w ||
            image->comps[i].h != image->comps[0].h ||
            image->comps[i].prec != image->comps[0].prec ||
            image->comps[i].sgnd != image->comps[0].sgnd)
            return FALSE;
    }

    return TRUE;
}

static void
copy_component_to_uint8 (opj_image_comp_t *component,
                         guint8 *dst,
                         guint width,
                         guint roi_y,
                         guint roi_height,
                         guint roi_step)
{
    gsize offset = 0;

    for (guint y = roi_y; y < roi_y + roi_height; y += roi_step) {
        for (guint x = 0; x < width; x++)
            dst[offset + x] = (guint8) component->data[y * width + x];

        offset += width;
    }
}

static void
copy_component_to_uint16 (opj_image_comp_t *component,
                          guint16 *dst,
                          guint width,
                          guint roi_y,
                          guint roi_height,
                          guint roi_step)
{
    gsize offset = 0;

    for (guint y = roi_y; y < roi_y + roi_height; y += roi_step) {
        for (guint x = 0; x < width; x++)
            dst[offset + x] = (guint16) component->data[y * width + x];

        offset += width;
    }
}

static void
copy_component_to_float (opj_image_comp_t *component,
                         gfloat *dst,
                         guint width,
                         guint roi_y,
                         guint roi_height,
                         guint roi_step)
{
    gsize offset = 0;

    for (guint y = roi_y; y < roi_y + roi_height; y += roi_step) {
        for (guint x = 0; x < width; x++)
            dst[offset + x] = (gfloat) component->data[y * width + x];

        offset += width;
    }
}

static gsize
ufo_jpeg2000_reader_read (UfoReader *reader,
                          UfoBuffer *buffer,
                          UfoRequisition *requisition,
                          guint roi_y,
                          guint roi_height,
                          guint roi_step,
                          guint image_step)
{
    UfoJpeg2000ReaderPrivate *priv;
    gpointer data;
    gsize plane_size;
    guint num_components;
    guint width;
    gboolean success;

    priv = UFO_JPEG2000_READER_GET_PRIVATE (reader);

    if (!priv->more || priv->image == NULL)
        return 0;

    success = opj_decode (priv->codec, priv->stream, priv->image) &&
              opj_end_decompress (priv->codec, priv->stream);

    if (!success) {
        g_warning ("Could not read JPEG 2000 image `%s'.", priv->filename);
        priv->more = FALSE;
        return 0;
    }

    data = ufo_buffer_get_host_array (buffer, NULL);
    width = requisition->dims[0];
    plane_size = requisition->dims[0] * requisition->dims[1];
    num_components = requisition->n_dims == 3 ? requisition->dims[2] : 1;

    for (guint component = 0; component < num_components; component++) {
        if (priv->bitdepth == UFO_BUFFER_DEPTH_8U)
            copy_component_to_uint8 (&priv->image->comps[component], ((guint8 *) data) + component * plane_size,
                                     width, roi_y, roi_height, roi_step);
        else if (priv->bitdepth == UFO_BUFFER_DEPTH_12U || priv->bitdepth == UFO_BUFFER_DEPTH_16U)
            copy_component_to_uint16 (&priv->image->comps[component], ((guint16 *) data) + component * plane_size,
                                      width, roi_y, roi_height, roi_step);
        else
            copy_component_to_float (&priv->image->comps[component], ((gfloat *) data) + component * plane_size,
                                     width, roi_y, roi_height, roi_step);
    }

    priv->more = FALSE;
    return 1;
}

static gboolean
ufo_jpeg2000_reader_get_meta (UfoReader *reader,
                              UfoRequisition *requisition,
                              gsize *num_images,
                              UfoBufferDepth *bitdepth,
                              GError **error)
{
    UfoJpeg2000ReaderPrivate *priv;
    guint precision;
    gboolean is_signed;
    guint num_components;

    priv = UFO_JPEG2000_READER_GET_PRIVATE (reader);
    g_assert (priv->image != NULL);

    if (!components_are_compatible (priv->image)) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "JPEG 2000 components in %s have incompatible dimensions or precision",
                     priv->filename);
        return FALSE;
    }

    num_components = priv->image->numcomps;

    if (num_components != 1 && num_components != 3) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "Unsupported number of JPEG 2000 components in %s: %u",
                     priv->filename, num_components);
        return FALSE;
    }

    requisition->n_dims = num_components == 3 ? 3 : 2;
    requisition->dims[0] = priv->image->comps[0].w;
    requisition->dims[1] = priv->image->comps[0].h;
    requisition->dims[2] = num_components == 3 ? 3 : 0;
    *num_images = 1;

    precision = priv->image->comps[0].prec;
    is_signed = priv->image->comps[0].sgnd != 0;

    if (is_signed || precision > 16)
        priv->bitdepth = UFO_BUFFER_DEPTH_32F;
    else if (precision <= 8)
        priv->bitdepth = UFO_BUFFER_DEPTH_8U;
    else if (precision <= 12)
        priv->bitdepth = UFO_BUFFER_DEPTH_12U;
    else
        priv->bitdepth = UFO_BUFFER_DEPTH_16U;

    *bitdepth = priv->bitdepth;
    return TRUE;
}

static void
ufo_jpeg2000_reader_finalize (GObject *object)
{
    UfoJpeg2000ReaderPrivate *priv;

    priv = UFO_JPEG2000_READER_GET_PRIVATE (object);
    cleanup_decoder (priv);
    g_free (priv->filename);
    priv->filename = NULL;

    G_OBJECT_CLASS (ufo_jpeg2000_reader_parent_class)->finalize (object);
}

static void
ufo_reader_interface_init (UfoReaderIface *iface)
{
    iface->can_open = ufo_jpeg2000_reader_can_open;
    iface->open = ufo_jpeg2000_reader_open;
    iface->close = ufo_jpeg2000_reader_close;
    iface->read = ufo_jpeg2000_reader_read;
    iface->get_meta = ufo_jpeg2000_reader_get_meta;
    iface->data_available = ufo_jpeg2000_reader_data_available;
}

static void
ufo_jpeg2000_reader_class_init (UfoJpeg2000ReaderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = ufo_jpeg2000_reader_finalize;

    g_type_class_add_private (gobject_class, sizeof (UfoJpeg2000ReaderPrivate));
}

static void
ufo_jpeg2000_reader_init (UfoJpeg2000Reader *self)
{
    UfoJpeg2000ReaderPrivate *priv = NULL;

    self->priv = priv = UFO_JPEG2000_READER_GET_PRIVATE (self);
    priv->filename = NULL;
    priv->codec = NULL;
    priv->stream = NULL;
    priv->image = NULL;
    priv->bitdepth = UFO_BUFFER_DEPTH_INVALID;
    priv->more = FALSE;
}
