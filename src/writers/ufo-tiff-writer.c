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

#include "config.h"

#include <tiffio.h>
#include <string.h>

#ifdef HAVE_JPEG2000
#include <openjpeg.h>
#endif

#include "writers/ufo-writer.h"
#include "writers/ufo-tiff-writer.h"


struct _UfoTiffWriterPrivate {
    TIFF *tiff;
    guint page;
    gboolean bigtiff;
#ifdef HAVE_JPEG2000
    gboolean jpeg2000;
    guint level;
    guint tile_size;
#endif
};

static void ufo_writer_interface_init (UfoWriterIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoTiffWriter, ufo_tiff_writer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_WRITER,
                                                ufo_writer_interface_init))

#define UFO_TIFF_WRITER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_TIFF_WRITER, UfoTiffWriterPrivate))

enum {
    PROP_0,
    PROP_BIGTIFF,
#ifdef HAVE_JPEG2000
    PROP_JPEG2000,
    PROP_LEVEL,
    PROP_TILE_SIZE,
#endif
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

#ifdef HAVE_JPEG2000
typedef struct {
    GByteArray *bytes;
    gsize offset;
} UfoJpeg2000Buffer;

static OPJ_SIZE_T
jpeg2000_stream_write (void *buffer,
                       OPJ_SIZE_T num_bytes,
                       void *user_data)
{
    UfoJpeg2000Buffer *output = user_data;
    gsize requested_size;

    requested_size = output->offset + num_bytes;

    if (requested_size > output->bytes->len)
        g_byte_array_set_size (output->bytes, requested_size);

    memcpy (output->bytes->data + output->offset, buffer, num_bytes);
    output->offset = requested_size;

    return num_bytes;
}

static OPJ_OFF_T
jpeg2000_stream_skip (OPJ_OFF_T num_bytes,
                      void *user_data)
{
    UfoJpeg2000Buffer *output = user_data;
    gssize requested_offset;

    requested_offset = (gssize) output->offset + num_bytes;

    if (requested_offset < 0)
        return -1;

    output->offset = requested_offset;

    if (output->offset > output->bytes->len)
        g_byte_array_set_size (output->bytes, output->offset);

    return num_bytes;
}

static OPJ_BOOL
jpeg2000_stream_seek (OPJ_OFF_T offset,
                      void *user_data)
{
    UfoJpeg2000Buffer *output = user_data;

    if (offset < 0)
        return OPJ_FALSE;

    output->offset = offset;

    if (output->offset > output->bytes->len)
        g_byte_array_set_size (output->bytes, output->offset);

    return OPJ_TRUE;
}

static gboolean
fill_jpeg2000_components (opj_image_t *jp2_image,
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

static gboolean
fill_jpeg2000_tile (gpointer tile_data,
                    UfoWriterImage *image,
                    guint tile_x,
                    guint tile_y,
                    UfoRequisition *tile_requisition,
                    guint num_components)
{
    gsize source_width;
    gsize source_height;
    gsize tile_width;
    gsize tile_height;

    source_width = image->requisition->dims[0];
    source_height = image->requisition->dims[1];
    tile_width = tile_requisition->dims[0];
    tile_height = tile_requisition->dims[1];

    if (image->depth == UFO_BUFFER_DEPTH_8U) {
        const guint8 *source = image->data;
        guint8 *destination = tile_data;

        for (gsize y = 0; y < tile_height; y++) {
            for (gsize x = 0; x < tile_width; x++) {
                gsize destination_index = (y * tile_width + x) * num_components;

                if (tile_x + x < source_width && tile_y + y < source_height) {
                    gsize source_index = ((tile_y + y) * source_width + tile_x + x) * num_components;

                    for (guint component = 0; component < num_components; component++)
                        destination[destination_index + component] = source[source_index + component];
                }
                else {
                    for (guint component = 0; component < num_components; component++)
                        destination[destination_index + component] = 0;
                }
            }
        }

        return TRUE;
    }

    if (image->depth == UFO_BUFFER_DEPTH_16U) {
        const guint16 *source = image->data;
        guint16 *destination = tile_data;

        for (gsize y = 0; y < tile_height; y++) {
            for (gsize x = 0; x < tile_width; x++) {
                gsize destination_index = (y * tile_width + x) * num_components;

                if (tile_x + x < source_width && tile_y + y < source_height) {
                    gsize source_index = ((tile_y + y) * source_width + tile_x + x) * num_components;

                    for (guint component = 0; component < num_components; component++)
                        destination[destination_index + component] = source[source_index + component];
                }
                else {
                    for (guint component = 0; component < num_components; component++)
                        destination[destination_index + component] = 0;
                }
            }
        }

        return TRUE;
    }

    return FALSE;
}

static GByteArray *
encode_jpeg2000_codestream (UfoWriterImage *image,
                            gboolean is_rgb,
                            guint level)
{
    opj_cparameters_t parameters;
    opj_image_cmptparm_t component_parameters[3];
    opj_image_t *jp2_image = NULL;
    opj_codec_t *codec = NULL;
    opj_stream_t *stream = NULL;
    UfoJpeg2000Buffer output;
    GByteArray *bytes = NULL;
    guint num_components;
    guint precision;
    guint threads;
    gboolean success = FALSE;

    num_components = is_rgb ? 3 : 1;

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

    jp2_image = opj_image_create (num_components,
                                  component_parameters,
                                  is_rgb ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY);

    if (jp2_image == NULL)
        goto cleanup;

    jp2_image->x0 = 0;
    jp2_image->y0 = 0;
    jp2_image->x1 = image->requisition->dims[0];
    jp2_image->y1 = image->requisition->dims[1];

    if (!fill_jpeg2000_components (jp2_image, image, num_components))
        goto cleanup;

    opj_set_default_encoder_parameters (&parameters);
    parameters.cod_format = 0;

    if (level > 0) {
        parameters.irreversible = 1;
        parameters.cp_fixed_quality = 1;
        parameters.tcp_numlayers = 1;
        parameters.tcp_distoratio[0] = level;
    }

    codec = opj_create_compress (OPJ_CODEC_J2K);
    if (codec == NULL)
        goto cleanup;

    if (!opj_setup_encoder (codec, &parameters, jp2_image))
        goto cleanup;

    threads = g_get_num_processors ();

    if (threads > 1 && !opj_codec_set_threads (codec, (int) threads))
        g_warning ("Could not enable %u OpenJPEG worker threads.", threads);

    bytes = g_byte_array_new ();
    output.bytes = bytes;
    output.offset = 0;

    stream = opj_stream_default_create (OPJ_FALSE);
    if (stream == NULL)
        goto cleanup;

    opj_stream_set_write_function (stream, jpeg2000_stream_write);
    opj_stream_set_skip_function (stream, jpeg2000_stream_skip);
    opj_stream_set_seek_function (stream, jpeg2000_stream_seek);
    opj_stream_set_user_data (stream, &output, NULL);

    success = opj_start_compress (codec, jp2_image, stream) &&
              opj_encode (codec, stream) &&
              opj_end_compress (codec, stream);

cleanup:
    if (stream != NULL)
        opj_stream_destroy (stream);

    if (codec != NULL)
        opj_destroy_codec (codec);

    if (jp2_image != NULL)
        opj_image_destroy (jp2_image);

    if (!success) {
        if (bytes != NULL)
            g_byte_array_unref (bytes);

        return NULL;
    }

    return bytes;
}

static gboolean
write_jpeg2000_tiles (UfoTiffWriterPrivate *priv,
                      UfoWriterImage *image,
                      gboolean is_rgb)
{
    UfoRequisition tile_requisition;
    UfoWriterImage tile_image;
    gpointer tile_data = NULL;
    guint num_components;
    guint bytes_per_sample;
    guint image_width;
    guint image_height;
    gboolean success = TRUE;

    num_components = is_rgb ? 3 : 1;
    bytes_per_sample = image->depth == UFO_BUFFER_DEPTH_8U ? 1 : 2;
    image_width = image->requisition->dims[0];
    image_height = image->requisition->dims[1];

    tile_data = g_malloc ((gsize) priv->tile_size * priv->tile_size * num_components * bytes_per_sample);

    tile_image = *image;
    tile_image.requisition = &tile_requisition;
    tile_image.data = tile_data;

    for (guint y = 0; y < image_height; y += priv->tile_size) {
        for (guint x = 0; x < image_width; x += priv->tile_size) {
            GByteArray *codestream;
            guint tile;
            tmsize_t written;

            tile_requisition.n_dims = is_rgb ? 3 : 2;
            tile_requisition.dims[0] = priv->tile_size;
            tile_requisition.dims[1] = priv->tile_size;
            tile_requisition.dims[2] = is_rgb ? 3 : 0;

            if (!fill_jpeg2000_tile (tile_data, image, x, y, &tile_requisition, num_components)) {
                success = FALSE;
                goto cleanup;
            }

            codestream = encode_jpeg2000_codestream (&tile_image, is_rgb, priv->level);

            if (codestream == NULL) {
                g_warning ("Could not encode TIFF tile with JPEG 2000 compression.");
                success = FALSE;
                goto cleanup;
            }

            tile = TIFFComputeTile (priv->tiff, x, y, 0, 0);
            written = TIFFWriteRawTile (priv->tiff, tile, codestream->data, codestream->len);
            g_byte_array_unref (codestream);

            if (written < 0) {
                g_warning ("Could not write JPEG 2000 TIFF tile.");
                success = FALSE;
                goto cleanup;
            }
        }
    }

cleanup:
    g_free (tile_data);
    return success;
}
#endif

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
    priv->tiff = TIFFOpen (filename, priv->bigtiff ? "w8" : "w");
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

#ifdef HAVE_JPEG2000
    if (priv->jpeg2000 &&
        image->depth != UFO_BUFFER_DEPTH_8U &&
        image->depth != UFO_BUFFER_DEPTH_16U) {
        image->depth = UFO_BUFFER_DEPTH_16U;
        ufo_writer_convert_inplace (image);
    }
#endif

    TIFFSetField (priv->tiff, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
    TIFFSetField (priv->tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (priv->tiff, TIFFTAG_IMAGEWIDTH, image->requisition->dims[0]);
    TIFFSetField (priv->tiff, TIFFTAG_IMAGELENGTH, image->requisition->dims[1]);
    TIFFSetField (priv->tiff, TIFFTAG_SAMPLESPERPIXEL, is_rgb ? 3 : 1);
#ifdef HAVE_JPEG2000
    if (!priv->jpeg2000 || priv->tile_size == 0)
#endif
        TIFFSetField (priv->tiff, TIFFTAG_ROWSPERSTRIP,
#ifdef HAVE_JPEG2000
                      priv->jpeg2000 ? image->requisition->dims[1] :
#endif
                      TIFFDefaultStripSize (priv->tiff, (guint32) - 1));
    TIFFSetField (priv->tiff, TIFFTAG_PHOTOMETRIC, is_rgb ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK);

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

#ifdef HAVE_JPEG2000
    if (priv->jpeg2000) {
        GByteArray *codestream;
        tmsize_t written;

        TIFFSetField (priv->tiff, TIFFTAG_COMPRESSION, COMPRESSION_JP2000);

        if (priv->tile_size > 0) {
            TIFFSetField (priv->tiff, TIFFTAG_TILEWIDTH, priv->tile_size);
            TIFFSetField (priv->tiff, TIFFTAG_TILELENGTH, priv->tile_size);

            if (!write_jpeg2000_tiles (priv, image, is_rgb))
                return;

            TIFFWriteDirectory (priv->tiff);
            priv->page++;
            return;
        }

        codestream = encode_jpeg2000_codestream (image, is_rgb, priv->level);

        if (codestream == NULL) {
            g_warning ("Could not encode TIFF page with JPEG 2000 compression.");
            return;
        }

        written = TIFFWriteRawStrip (priv->tiff, 0, codestream->data, codestream->len);
        g_byte_array_unref (codestream);

        if (written < 0) {
            g_warning ("Could not write JPEG 2000 TIFF strip.");
            return;
        }

        TIFFWriteDirectory (priv->tiff);
        priv->page++;
        return;
    }
#endif

    TIFFSetField (priv->tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    stride = image->requisition->dims[0] * bits_per_sample / 8;
    stride *= is_rgb ? image->requisition->dims[2] : 1;
    buff = (gchar *) image->data;

    for (guint y = 0; y < image->requisition->dims[1]; y++) {
        TIFFWriteScanline (priv->tiff, buff, y, 0);
        buff += stride;
    }

    TIFFWriteDirectory (priv->tiff);
    priv->page++;
}

static void
ufo_tiff_writer_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoTiffWriterPrivate *priv = UFO_TIFF_WRITER_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_BIGTIFF:
            priv->bigtiff = g_value_get_boolean (value);
            break;
#ifdef HAVE_JPEG2000
        case PROP_JPEG2000:
            priv->jpeg2000 = g_value_get_boolean (value);
            break;
        case PROP_LEVEL:
            priv->level = g_value_get_uint (value);
            break;
        case PROP_TILE_SIZE:
            priv->tile_size = g_value_get_uint (value);
            break;
#endif
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}
static void
ufo_tiff_writer_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoTiffWriterPrivate *priv = UFO_TIFF_WRITER_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_BIGTIFF:
            g_value_set_boolean (value, priv->bigtiff);
            break;
#ifdef HAVE_JPEG2000
        case PROP_JPEG2000:
            g_value_set_boolean (value, priv->jpeg2000);
            break;
        case PROP_LEVEL:
            g_value_set_uint (value, priv->level);
            break;
        case PROP_TILE_SIZE:
            g_value_set_uint (value, priv->tile_size);
            break;
#endif
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
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

    gobject_class->set_property = ufo_tiff_writer_set_property;
    gobject_class->get_property = ufo_tiff_writer_get_property;
    gobject_class->finalize = ufo_tiff_writer_finalize;

    properties[PROP_BIGTIFF] =
        g_param_spec_boolean("bigtiff",
            "Write BigTiff format",
            "Write BigTiff format",
            TRUE,
            G_PARAM_READWRITE);

#ifdef HAVE_JPEG2000
    properties[PROP_JPEG2000] =
        g_param_spec_boolean("jpeg2000",
            "Compress TIFF pages with JPEG 2000",
            "Compress TIFF pages with JPEG 2000",
            FALSE,
            G_PARAM_READWRITE);

    properties[PROP_LEVEL] =
        g_param_spec_uint("level",
            "JPEG 2000 quality level",
            "JPEG 2000 quality level. 0 is lossless; 1 to 100 enable progressively higher lossy quality",
            0, 100, 0,
            G_PARAM_READWRITE);

    properties[PROP_TILE_SIZE] =
        g_param_spec_uint("tile-size",
            "Square TIFF tile size",
            "Square tile size for JPEG 2000-compressed TIFF output. 0 writes one strip per page.",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

#endif

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof (UfoTiffWriterPrivate));
}

static void
ufo_tiff_writer_init (UfoTiffWriter *self)
{
    UfoTiffWriterPrivate *priv = NULL;

    self->priv = priv = UFO_TIFF_WRITER_GET_PRIVATE (self);
    priv->tiff = NULL;
    priv->bigtiff = TRUE;
#ifdef HAVE_JPEG2000
    priv->jpeg2000 = FALSE;
    priv->level = 0;
    priv->tile_size = 0;
#endif
}
