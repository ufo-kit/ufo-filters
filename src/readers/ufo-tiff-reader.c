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

#include <string.h>
#include <tiffio.h>

#ifdef HAVE_JPEG2000
#include <openjpeg.h>
#endif

#include "readers/ufo-reader.h"
#include "readers/ufo-tiff-reader.h"


struct _UfoTiffReaderPrivate {
    TIFF    *tiff;
    gboolean more;
    gsize num_images;
    guint jpeg2000_threads;
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

void
ufo_tiff_reader_set_jpeg2000_threads (UfoTiffReader *reader,
                                      guint threads)
{
    reader->priv->jpeg2000_threads = threads;
}

#ifdef HAVE_JPEG2000
typedef struct {
    const OPJ_BYTE *data;
    gsize size;
    gsize offset;
} UfoJpeg2000Input;

static OPJ_SIZE_T
jpeg2000_stream_read (void *buffer,
                      OPJ_SIZE_T num_bytes,
                      void *user_data)
{
    UfoJpeg2000Input *input = user_data;
    gsize available;
    gsize to_copy;

    if (input->offset >= input->size)
        return (OPJ_SIZE_T) -1;

    available = input->size - input->offset;
    to_copy = MIN ((gsize) num_bytes, available);

    memcpy (buffer, input->data + input->offset, to_copy);
    input->offset += to_copy;

    return to_copy;
}

static OPJ_OFF_T
jpeg2000_stream_skip (OPJ_OFF_T num_bytes,
                      void *user_data)
{
    UfoJpeg2000Input *input = user_data;
    gssize requested_offset;

    requested_offset = (gssize) input->offset + num_bytes;

    if (requested_offset < 0)
        return -1;

    input->offset = MIN ((gsize) requested_offset, input->size);
    return num_bytes;
}

static OPJ_BOOL
jpeg2000_stream_seek (OPJ_OFF_T offset,
                      void *user_data)
{
    UfoJpeg2000Input *input = user_data;

    if (offset < 0 || (gsize) offset > input->size)
        return OPJ_FALSE;

    input->offset = offset;
    return OPJ_TRUE;
}

static gboolean
jpeg2000_components_are_compatible (opj_image_t *image)
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

#endif

static gboolean
ufo_tiff_reader_can_open (UfoReader *reader,
                         const gchar *filename)
{
    return g_str_has_suffix (filename, ".tiff") || g_str_has_suffix (filename, ".tif");
}

static gboolean
ufo_tiff_reader_open (UfoReader *reader,
                      const gchar *filename,
                      guint start,
                      GError **error)
{
    UfoTiffReaderPrivate *priv;

    priv = UFO_TIFF_READER_GET_PRIVATE (reader);
    priv->num_images = 0;
    priv->tiff = TIFFOpen (filename, "r");
    priv->more = FALSE;

    if (priv->tiff == NULL) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "Cannot open %s", filename);
        return FALSE;
    }

	do {
	    priv->num_images++;
	} while (TIFFReadDirectory(priv->tiff));

    if (start < priv->num_images) {
        priv->more = TRUE;
        if (TIFFSetDirectory (priv->tiff, start) != 1) {
            g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                         "Cannot find first image in %s", filename);
            return FALSE;
        }
    }

    return TRUE;
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
read_data (UfoTiffReaderPrivate *priv,
           UfoBuffer *buffer,
           UfoRequisition *requisition,
           guint16 bits,
           guint roi_y,
           guint roi_height,
           guint roi_step)
{
    gchar *dst;
    gsize step;
    gsize offset;

    step = requisition->dims[0] * bits / 8;
    dst = (gchar *) ufo_buffer_get_host_array (buffer, NULL);
    offset = 0;

    if (requisition->n_dims == 3) {
        /* RGB data */
        gchar *src;
        gsize plane_size;

        /* Allow things like roi_height=1 and roi_step=20 */
        plane_size = step * ((roi_height - 1) / roi_step + 1);
        src = g_new0 (gchar, step * 3);

        for (guint i = roi_y; i < roi_y + roi_height; i += roi_step) {
            guint xd = 0;
            guint xs = 0;

            TIFFReadScanline (priv->tiff, src, i, 0);

            for (; xd < requisition->dims[0]; xd += 1, xs += 3) {
                dst[offset + xd] = src[xs];
                dst[offset + plane_size + xd] = src[xs + 1];
                dst[offset + 2 * plane_size + xd] = src[xs + 2];
            }

            offset += step;
        }

        g_free (src);
    }
    else {
        for (guint i = roi_y; i < roi_y + roi_height; i += roi_step) {
            TIFFReadScanline (priv->tiff, dst + offset, i, 0);
            offset += step;
        }
    }
}

static void
read_64_bit_data (UfoTiffReaderPrivate *priv,
                  UfoBuffer *buffer,
                  UfoRequisition *requisition,
                  guint roi_y,
                  guint roi_height,
                  guint roi_step)
{
    gdouble *src;
    gfloat *dst;

    dst = ufo_buffer_get_host_array (buffer, NULL);
    src = g_new0 (gdouble, requisition->dims[0]);

    for (guint i = roi_y; i < roi_y + roi_height; i += roi_step) {
        TIFFReadScanline (priv->tiff, src, i, 0);

        for (guint j = 0; j < requisition->dims[0]; j++)
            dst[j] = (gfloat) src[j];

        dst += requisition->dims[0];
    }

    g_free (src);
}

static void
read_tiled_data (UfoTiffReaderPrivate *priv,
                 UfoBuffer *buffer,
                 UfoRequisition *requisition,
                 guint16 bits,
                 guint roi_y,
                 guint roi_height,
                 guint roi_step)
{
    guint32 image_width;
    guint32 image_height;
    guint32 tile_width;
    guint32 tile_height;
    guint32 samples;
    guint bytes_per_sample;
    guint8 *tile_data;
    guint8 *dst;
    tmsize_t tile_size;
    gsize plane_size;
    guint roi_end;

    if (bits % 8 != 0) {
        g_warning ("Can not read tiled TIFF images with %u bits per sample.", bits);
        return;
    }

    TIFFGetField (priv->tiff, TIFFTAG_IMAGEWIDTH, &image_width);
    TIFFGetField (priv->tiff, TIFFTAG_IMAGELENGTH, &image_height);
    TIFFGetField (priv->tiff, TIFFTAG_TILEWIDTH, &tile_width);
    TIFFGetField (priv->tiff, TIFFTAG_TILELENGTH, &tile_height);
    TIFFGetField (priv->tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);

    if (samples != 1 && samples != 3) {
        g_warning ("Can not read tiled TIFF images with %u samples per pixel.", samples);
        return;
    }

    tile_size = TIFFTileSize (priv->tiff);

    if (tile_size <= 0)
        return;

    bytes_per_sample = bits / 8;
    dst = (guint8 *) ufo_buffer_get_host_array (buffer, NULL);
    tile_data = g_malloc (tile_size);
    plane_size = requisition->dims[0] * requisition->dims[1] * bytes_per_sample;
    roi_end = roi_y + roi_height;

    for (guint y = 0; y < image_height; y += tile_height) {
        if (y >= roi_end || y + tile_height <= roi_y)
            continue;

        for (guint x = 0; x < image_width; x += tile_width) {
            guint first_row;
            guint last_row;
            guint first_column;
            guint last_column;

            if (TIFFReadTile (priv->tiff, tile_data, x, y, 0, 0) < 0)
                continue;

            first_row = MAX (roi_y, y);
            last_row = MIN (roi_end, y + tile_height);
            first_column = x;
            last_column = MIN ((guint) image_width, x + tile_width);

            if ((first_row - roi_y) % roi_step)
                first_row += roi_step - ((first_row - roi_y) % roi_step);

            for (guint row = first_row; row < last_row; row += roi_step) {
                guint src_row = row - y;
                guint dst_row = (row - roi_y) / roi_step;

                for (guint column = first_column; column < last_column; column++) {
                    guint src_column = column - x;
                    gsize src_index = (src_row * tile_width + src_column) * samples * bytes_per_sample;

                    if (samples == 1) {
                        gsize dst_index = (dst_row * requisition->dims[0] + column) * bytes_per_sample;
                        memcpy (dst + dst_index, tile_data + src_index, bytes_per_sample);
                    }
                    else {
                        for (guint sample = 0; sample < samples; sample++) {
                            gsize dst_index = sample * plane_size +
                                              (dst_row * requisition->dims[0] + column) * bytes_per_sample;
                            memcpy (dst + dst_index,
                                    tile_data + src_index + sample * bytes_per_sample,
                                    bytes_per_sample);
                        }
                    }
                }
            }
        }
    }

    g_free (tile_data);
}

#ifdef HAVE_JPEG2000
static gboolean
decode_jpeg2000_raw_chunk (UfoTiffReaderPrivate *priv,
                           guint chunk,
                           gboolean tiled,
                           guint8 **raw_data,
                           tmsize_t *raw_size)
{
    tmsize_t chunk_size;

    if (tiled) {
        uint64_t *byte_counts = NULL;

        if (!TIFFGetField (priv->tiff, TIFFTAG_TILEBYTECOUNTS, &byte_counts) ||
            byte_counts == NULL ||
            byte_counts[chunk] > (uint64_t) G_MAXSSIZE)
            return FALSE;

        chunk_size = (tmsize_t) byte_counts[chunk];
    }
    else {
        chunk_size = TIFFRawStripSize (priv->tiff, chunk);
    }

    if (chunk_size <= 0)
        return FALSE;

    *raw_data = g_malloc (chunk_size);
    *raw_size = tiled ?
        TIFFReadRawTile (priv->tiff, chunk, *raw_data, chunk_size) :
        TIFFReadRawStrip (priv->tiff, chunk, *raw_data, chunk_size);

    if (*raw_size <= 0) {
        g_free (*raw_data);
        *raw_data = NULL;
        return FALSE;
    }

    return TRUE;
}

static opj_image_t *
decode_jpeg2000_chunk (UfoTiffReaderPrivate *priv,
                       guint chunk,
                       gboolean tiled)
{
    guint8 *raw_data = NULL;
    tmsize_t raw_size = 0;
    UfoJpeg2000Input input;
    opj_dparameters_t parameters;
    opj_stream_t *stream = NULL;
    opj_codec_t *codec = NULL;
    opj_image_t *image = NULL;
    guint threads;
    gboolean success = FALSE;

    if (!decode_jpeg2000_raw_chunk (priv, chunk, tiled, &raw_data, &raw_size))
        goto cleanup;

    input.data = raw_data;
    input.size = raw_size;
    input.offset = 0;

    stream = opj_stream_default_create (OPJ_TRUE);
    if (stream == NULL)
        goto cleanup;

    opj_stream_set_read_function (stream, jpeg2000_stream_read);
    opj_stream_set_skip_function (stream, jpeg2000_stream_skip);
    opj_stream_set_seek_function (stream, jpeg2000_stream_seek);
    opj_stream_set_user_data (stream, &input, NULL);
    opj_stream_set_user_data_length (stream, raw_size);

    codec = opj_create_decompress (OPJ_CODEC_J2K);
    if (codec == NULL)
        goto cleanup;

    opj_set_default_decoder_parameters (&parameters);

    if (!opj_setup_decoder (codec, &parameters))
        goto cleanup;

    threads = priv->jpeg2000_threads == 0
              ? g_get_num_processors ()
              : priv->jpeg2000_threads;

    if (threads > 1 && !opj_codec_set_threads (codec, (int) threads))
        g_warning ("Could not enable %u OpenJPEG worker threads.", threads);

    success = opj_read_header (stream, codec, &image) &&
              opj_decode (codec, stream, image) &&
              opj_end_decompress (codec, stream);

    if (!success || image == NULL || !jpeg2000_components_are_compatible (image))
        goto cleanup;

cleanup:
    if (!success && image != NULL) {
        opj_image_destroy (image);
        image = NULL;
    }

    if (codec != NULL)
        opj_destroy_codec (codec);

    if (stream != NULL)
        opj_stream_destroy (stream);

    g_free (raw_data);
    return image;
}

static gboolean
copy_jpeg2000_chunk (opj_image_t *image,
                     UfoBuffer *buffer,
                     UfoRequisition *requisition,
                     guint16 bits,
                     guint chunk_x,
                     guint chunk_y,
                     guint roi_y,
                     guint roi_height,
                     guint roi_step)
{
    gpointer data;
    gsize plane_size;
    guint num_components;
    guint width;
    guint chunk_width;
    guint chunk_height;
    guint chunk_end;
    guint roi_end;
    guint first_row;
    guint first_column;
    guint last_column;

    data = ufo_buffer_get_host_array (buffer, NULL);
    width = requisition->dims[0];
    plane_size = requisition->dims[0] * requisition->dims[1];
    num_components = requisition->n_dims == 3 ? requisition->dims[2] : 1;

    if (image->numcomps < num_components)
        return FALSE;

    chunk_width = image->comps[0].w;
    chunk_height = image->comps[0].h;
    chunk_end = chunk_y + chunk_height;
    roi_end = roi_y + roi_height;
    first_row = MAX (roi_y, chunk_y);
    first_column = chunk_x;
    last_column = MIN (width, chunk_x + chunk_width);

    if (first_row >= chunk_end || first_row >= roi_end || first_column >= last_column)
        return TRUE;

    if ((first_row - roi_y) % roi_step)
        first_row += roi_step - ((first_row - roi_y) % roi_step);

    for (guint component = 0; component < num_components; component++) {
        for (guint row = first_row; row < MIN (chunk_end, roi_end); row += roi_step) {
            guint src_row = row - chunk_y;
            guint dst_row = (row - roi_y) / roi_step;

            for (guint x = first_column; x < last_column; x++) {
                gsize src_index = src_row * image->comps[component].w + (x - chunk_x);
                gsize dst_index = component * plane_size + dst_row * width + x;

                if (bits <= 8)
                    ((guint8 *) data)[dst_index] = (guint8) image->comps[component].data[src_index];
                else if (bits <= 16)
                    ((guint16 *) data)[dst_index] = (guint16) image->comps[component].data[src_index];
                else
                    ((gfloat *) data)[dst_index] = (gfloat) image->comps[component].data[src_index];
            }
        }
    }

    return TRUE;
}

static void
read_jpeg2000_strips (UfoTiffReaderPrivate *priv,
                      UfoBuffer *buffer,
                      UfoRequisition *requisition,
                      guint16 bits,
                      guint roi_y,
                      guint roi_height,
                      guint roi_step)
{
    guint32 num_strips;
    guint32 rows_per_strip;
    guint32 image_height;

    num_strips = TIFFNumberOfStrips (priv->tiff);
    TIFFGetField (priv->tiff, TIFFTAG_IMAGELENGTH, &image_height);
    TIFFGetFieldDefaulted (priv->tiff, TIFFTAG_ROWSPERSTRIP, &rows_per_strip);

    for (guint strip = 0; strip < num_strips; strip++) {
        opj_image_t *image;
        guint strip_start;

        strip_start = strip * rows_per_strip;

        if (strip_start >= image_height)
            break;

        if (strip_start >= roi_y + roi_height || strip_start + rows_per_strip <= roi_y)
            continue;

        image = decode_jpeg2000_chunk (priv, strip, FALSE);

        if (image == NULL) {
            g_warning ("Could not decode JPEG 2000 compressed TIFF strip %u.", strip);
            continue;
        }

        if (!copy_jpeg2000_chunk (image, buffer, requisition, bits, 0, strip_start, roi_y, roi_height, roi_step))
            g_warning ("Could not copy JPEG 2000 compressed TIFF strip %u.", strip);

        opj_image_destroy (image);
    }
}

static void
read_jpeg2000_tiles (UfoTiffReaderPrivate *priv,
                     UfoBuffer *buffer,
                     UfoRequisition *requisition,
                     guint16 bits,
                     guint roi_y,
                     guint roi_height,
                     guint roi_step)
{
    guint32 image_width;
    guint32 image_height;
    guint32 tile_width;
    guint32 tile_height;
    guint roi_end;

    TIFFGetField (priv->tiff, TIFFTAG_IMAGEWIDTH, &image_width);
    TIFFGetField (priv->tiff, TIFFTAG_IMAGELENGTH, &image_height);
    TIFFGetField (priv->tiff, TIFFTAG_TILEWIDTH, &tile_width);
    TIFFGetField (priv->tiff, TIFFTAG_TILELENGTH, &tile_height);

    roi_end = roi_y + roi_height;

    for (guint y = 0; y < image_height; y += tile_height) {
        if (y >= roi_end || y + tile_height <= roi_y)
            continue;

        for (guint x = 0; x < image_width; x += tile_width) {
            opj_image_t *image;
            guint tile;

            tile = TIFFComputeTile (priv->tiff, x, y, 0, 0);
            image = decode_jpeg2000_chunk (priv, tile, TRUE);

            if (image == NULL) {
                g_warning ("Could not decode JPEG 2000 compressed TIFF tile %u.", tile);
                continue;
            }

            if (!copy_jpeg2000_chunk (image, buffer, requisition, bits, x, y, roi_y, roi_height, roi_step))
                g_warning ("Could not copy JPEG 2000 compressed TIFF tile %u.", tile);

            opj_image_destroy (image);
        }
    }
}

static void
read_jpeg2000_data (UfoTiffReaderPrivate *priv,
                    UfoBuffer *buffer,
                    UfoRequisition *requisition,
                    guint16 bits,
                    guint roi_y,
                    guint roi_height,
                    guint roi_step)
{
    if (TIFFIsTiled (priv->tiff))
        read_jpeg2000_tiles (priv, buffer, requisition, bits, roi_y, roi_height, roi_step);
    else
        read_jpeg2000_strips (priv, buffer, requisition, bits, roi_y, roi_height, roi_step);
}
#endif

static gsize
ufo_tiff_reader_read (UfoReader *reader,
                      UfoBuffer *buffer,
                      UfoRequisition *requisition,
                      guint roi_y,
                      guint roi_height,
                      guint roi_step,
                      guint image_step)
{
    UfoTiffReaderPrivate *priv;
    guint16 bits;
    guint16 compression;
    gsize num_read = 0;

    priv = UFO_TIFF_READER_GET_PRIVATE (reader);

    TIFFGetField (priv->tiff, TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetFieldDefaulted (priv->tiff, TIFFTAG_COMPRESSION, &compression);

#ifdef HAVE_JPEG2000
    if (compression == COMPRESSION_JP2000)
        read_jpeg2000_data (priv, buffer, requisition, bits, roi_y, roi_height, roi_step);
    else
#endif
    if (TIFFIsTiled (priv->tiff))
        read_tiled_data (priv, buffer, requisition, bits, roi_y, roi_height, roi_step);
    else
    if (bits == 64)
        read_64_bit_data (priv, buffer, requisition, roi_y, roi_height, roi_step);
    else
        read_data (priv, buffer, requisition, bits, roi_y, roi_height, roi_step);

    do {
        priv->more = TIFFReadDirectory (priv->tiff) == 1;
        num_read++;
        if (!priv->more) {
            break;
        }
    } while (num_read < image_step);

    return num_read;
}

static gboolean
ufo_tiff_reader_get_meta (UfoReader *reader,
                          UfoRequisition *requisition,
                          gsize *num_images,
                          UfoBufferDepth *bitdepth,
                          GError **error)
{
    UfoTiffReaderPrivate *priv;
    guint32 width;
    guint32 height;
    guint32 samples;
    guint16 bits_per_sample;

    priv = UFO_TIFF_READER_GET_PRIVATE (reader);
    g_assert (priv->tiff != NULL);

    TIFFGetField (priv->tiff, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField (priv->tiff, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetField (priv->tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
    TIFFGetField (priv->tiff, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);

    requisition->n_dims = samples == 3 ? 3 : 2;
    requisition->dims[0] = (gsize) width;
    requisition->dims[1] = (gsize) height;
    requisition->dims[2] = samples == 3 ? 3 : 0;
    *num_images = priv->num_images;

    switch (bits_per_sample) {
        case 8:
            *bitdepth = UFO_BUFFER_DEPTH_8U;
            break;
        case 12:
            *bitdepth = UFO_BUFFER_DEPTH_12U;
            break;
        case 16:
            *bitdepth = UFO_BUFFER_DEPTH_16U;
            break;
        default:
            *bitdepth = UFO_BUFFER_DEPTH_32F;
    }

    return TRUE;
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
    priv->jpeg2000_threads = 0;
    TIFFSetWarningHandler(NULL);
}
