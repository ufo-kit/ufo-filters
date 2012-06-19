#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
#include <glob.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include <ufo/ufo-resource-manager.h>

#include "ufo-filter-reader.h"

/**
 * SECTION:ufo-filter-reader
 * @Short_description: Read TIFF and EDF files
 * @Title: reader 
 *
 * The reader node loads single files from disk and provides them as a stream in
 * output "image". The nominal resolution can be decreased by specifying the
 * #UfoFilterReader:x and #UfoFilterReader:y coordinates, and the
 * #UfoFilterReader:width and #UfoFilterReader:height of a region of interest.
 */

struct _UfoFilterReaderPrivate {
    gchar *path;
    gint count;
    gint current_count;
    gint nth;
    gboolean blocking;
    gboolean normalize;
    gboolean more_pages;
    GSList *filenames;
    GSList *current_filename;
    TIFF *current_tiff;
    gpointer frame_buffer;

    gboolean roi;
    guint roi_x;
    guint roi_y;
    guint roi_width;
    guint roi_height;
};

G_DEFINE_TYPE(UfoFilterReader, ufo_filter_reader, UFO_TYPE_FILTER_SOURCE)

#define UFO_FILTER_READER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_READER, UfoFilterReaderPrivate))

enum {
    PROP_0,
    PROP_PATH,
    PROP_COUNT,
    PROP_BLOCKING,
    PROP_NTH,
    PROP_NORMALIZE,
    PROP_ROI,
    PROP_ROI_X,
    PROP_ROI_Y,
    PROP_ROI_WIDTH,
    PROP_ROI_HEIGHT,
    N_PROPERTIES
};

static GParamSpec *reader_properties[N_PROPERTIES] = { NULL, };

static gpointer image_buffer = NULL;
static gsize image_size = 0;

static gboolean 
filter_decode_tiff(TIFF *tif, void *buffer)
{
    const tsize_t strip_size = TIFFStripSize(tif);
    const guint n_strips = TIFFNumberOfStrips(tif);
    int offset = 0;
    tsize_t result = 0;

    for (guint strip = 0; strip < n_strips; strip++) {
        result = TIFFReadEncodedStrip(tif, strip, ((gchar *) buffer) +offset, strip_size);
        if (result == -1)
            return FALSE;
        offset += result;
    }
    return TRUE;
}

/**
 * read_tiff:
 *
 * Returns: TRUE if more frames can be read from @tif
 */
static gboolean 
read_tiff(TIFF *tif, gpointer *buffer, guint16 *bytes_per_sample, guint16 *samples_per_pixel, guint32 *width, guint32 *height)
{
    guint16 bits_per_sample = 8;
    TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);
    TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, samples_per_pixel);
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, height);

    if (*samples_per_pixel > 1)
        g_warning("TIFF has %i samples per pixel", *samples_per_pixel);

    *bytes_per_sample = bits_per_sample >> 3;
    gsize size = *bytes_per_sample * (*width) * (*height);

    if ((*buffer == NULL) || (image_size != size)) {
        *buffer = g_malloc0(size); 
        image_size = size;
    }

    if (!filter_decode_tiff(tif, *buffer))
        goto error_close;

    return TIFFReadDirectory(tif) == 1;

error_close:
    *buffer = NULL;
    return FALSE;
}

static void *
load_edf(const gchar *filename, guint16 *bytes_per_sample, guint16 *samples_per_pixel, guint32 *width, guint32 *height)
{
    FILE *fp = fopen(filename, "rb");  
    gchar *header = g_malloc(1024);

    size_t num_bytes = fread(header, 1, 1024, fp);
    if (num_bytes != 1024) {
        g_free(header);
        fclose(fp);
        return NULL;
    }

    gchar **tokens = g_strsplit(header, ";", 0);
    gboolean big_endian = FALSE;
    int index = 0;
    guint w = 0, h = 0, size = 0;

    while (tokens[index] != NULL) {
        gchar **key_value = g_strsplit(tokens[index], "=", 0);
        if (g_strcmp0(g_strstrip(key_value[0]), "Dim_1") == 0)
            w = (guint) atoi(key_value[1]);
        else if (g_strcmp0(g_strstrip(key_value[0]), "Dim_2") == 0)
            h = (guint) atoi(key_value[1]);
        else if (g_strcmp0(g_strstrip(key_value[0]), "Size") == 0)
            size = (guint) atoi(key_value[1]);
        else if ((g_strcmp0(g_strstrip(key_value[0]), "ByteOrder") == 0) &&
                 (g_strcmp0(g_strstrip(key_value[1]), "HighByteFirst") == 0))
            big_endian = TRUE;
        g_strfreev(key_value);
        index++;
    }

    g_strfreev(tokens);
    g_free(header);
    gsize expected_size = w * h * sizeof(float);

    if (expected_size != size) {
        g_warning("header value size is %i bytes rather than %lu bytes", size, expected_size);
        size = (guint) expected_size;
    }

    *bytes_per_sample = 4;
    *samples_per_pixel = 1;
    *width = w;
    *height = h;

    /* Skip header */
    fseek(fp, 0L, SEEK_END);
    gssize file_size = (gssize) ftell(fp);
    fseek(fp, file_size - size, SEEK_SET);

    /* Read data */
    if ((image_buffer == NULL) || (image_size != size)) {
        image_buffer = g_malloc(size);
        image_size = size;
    }

    num_bytes = fread(image_buffer, 1, size, fp);
    fclose(fp);
    if (num_bytes != size) {
        g_free(image_buffer);
        return NULL;
    }

    if ((G_BYTE_ORDER == G_LITTLE_ENDIAN) && big_endian) {
        guint32 *data = (guint32 *) image_buffer;    
        for (guint i = 0; i < w*h; i++)
            data[i] = g_ntohl(data[i]);
    }
    return image_buffer;
}

static GSList *
read_filenames(UfoFilterReaderPrivate *priv)
{
    GSList *result = NULL;
    glob_t glob_vector;
    guint i = (priv->nth < 0) ? 0 : (guint) priv->nth;
    glob(priv->path, GLOB_MARK | GLOB_TILDE, NULL, &glob_vector);

    while (i < glob_vector.gl_pathc)
        result = g_slist_append(result, g_strdup(glob_vector.gl_pathv[i++]));

    globfree(&glob_vector);
    return result;
}

static void 
push_data(UfoFilterReaderPrivate *priv, UfoBuffer *output, guint src_width, guint src_height, guint bytes_per_sample)
{
    if (!priv->roi) {
        ufo_buffer_set_host_array(output, priv->frame_buffer, bytes_per_sample * src_width * src_height, NULL);

        if (bytes_per_sample < 4)
            ufo_buffer_reinterpret(output, bytes_per_sample << 3, src_width * src_height, priv->normalize);
    }
    else {
        guint x1 = priv->roi_x, y1 = priv->roi_y;
        guint x2 = x1 + priv->roi_width, y2 = y1 + priv->roi_height;

        /* Don't do anything if we are completely out of bounds */
        if (x1 <= src_width && y1 <= src_height) {

            guint rd_width = x2 > src_width ? src_width - x1 : priv->roi_width;
            guint rd_height = y2 > src_height ? src_height - y1 : priv->roi_height;
            gfloat *in_data = (gfloat *) priv->frame_buffer;
            gfloat *out_data = ufo_buffer_get_host_array(output, NULL);

            if (rd_width == src_width) {
                g_memmove(out_data, in_data + y1*src_width, 
                        rd_width * rd_height * sizeof(gfloat));
            }
            else {
                for (guint y = 0; y < rd_height; y++) {
                    g_memmove(out_data + y*priv->roi_width, in_data + (y + y1)*src_width + x1, 
                            rd_width * sizeof(gfloat));
                }
            }

            if (bytes_per_sample < 4)
                g_warning("Region of interest with non-float data is not yet supported!");
        }
    }
}

static gpointer 
load_tiff(UfoFilterReaderPrivate *priv, guint16 *bytes_per_sample, guint16 *samples_per_pixel, guint *src_width, guint *src_height)
{
    gpointer frame_buffer = NULL;
    priv->current_tiff = TIFFOpen((char *) priv->current_filename->data, "r");

    if (priv->current_tiff == NULL)
        return NULL;

    priv->more_pages = read_tiff(priv->current_tiff, &frame_buffer,
            bytes_per_sample, samples_per_pixel, src_width, src_height);

    return frame_buffer;
}

static GError *
ufo_filter_reader_initialize(UfoFilterSource *filter, guint **dims)
{
    UfoFilterReaderPrivate *priv = UFO_FILTER_READER_GET_PRIVATE(filter);
    GError *error = NULL;

    priv->filenames = read_filenames(priv);
    priv->current_filename = priv->filenames;
    priv->current_count = 0;
    priv->count = priv->count == -1 ? G_MAXINT : priv->count;

    if (priv->filenames != NULL) {
        const gchar *name = (gchar *) priv->current_filename->data;
        guint width, height;
        guint16 bytes_per_sample, samples_per_pixel;

        if (g_str_has_suffix(name, "tif")) {
            TIFF *tif = TIFFOpen(name, "r");
            read_tiff(tif, &priv->frame_buffer, &bytes_per_sample, &samples_per_pixel, &width, &height);
            TIFFClose(tif);
        }
        else
            priv->frame_buffer = load_edf(name, &bytes_per_sample, &samples_per_pixel, &width, &height);

        if (!priv->roi || (priv->roi_width == 0) || (priv->roi_height == 0)) {
            dims[0][0] = width;
            dims[0][1] = height;
        }
        else {
            dims[0][0] = priv->roi_width;
            dims[0][1] = priv->roi_height;
        }
    }
    else {
        g_set_error(&error, UFO_FILTER_ERROR, UFO_FILTER_ERROR_INITIALIZATION, 
                "Path does not match any files");
    }

    return error;
}

static gboolean
ufo_filter_reader_generate_cpu(UfoFilterSource *filter, UfoBuffer *results[], gpointer cmd_queue, GError **error)
{
    UfoFilterReaderPrivate *priv = UFO_FILTER_READER_GET_PRIVATE(filter);
    guint src_width, src_height;
    guint16 bytes_per_sample, samples_per_pixel;

    if ((priv->current_count < priv->count) && (priv->current_filename != NULL)) {
        /* Do we have more images to read from the last open multi TIFF? */
        if (priv->more_pages) {
            priv->more_pages = read_tiff(priv->current_tiff, &priv->frame_buffer, 
                    &bytes_per_sample, &samples_per_pixel, &src_width, &src_height);
        } 
        else {
            const gchar *name = (gchar *) priv->current_filename->data;

            if (g_str_has_suffix(name, "tif")) {
                if (priv->current_tiff != NULL)
                    TIFFClose(priv->current_tiff);

                priv->frame_buffer = load_tiff(priv, &bytes_per_sample, &samples_per_pixel, &src_width, &src_height);
            }
            else
                priv->frame_buffer = load_edf(name, &bytes_per_sample, &samples_per_pixel, &src_width, &src_height);
        }

        if (priv->frame_buffer == NULL)
            return FALSE;

        push_data(priv, results[0], src_width, src_height, bytes_per_sample);

        if (!priv->more_pages)
            priv->current_filename = g_slist_next(priv->current_filename);

        priv->current_count++;
    }
    else
        return FALSE;

    return TRUE;
}

static void 
ufo_filter_reader_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterReaderPrivate *priv = UFO_FILTER_READER_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_PATH:
            g_free(priv->path);
            priv->path = g_value_dup_string(value);
            break;
        case PROP_COUNT:
            priv->count = g_value_get_int(value);
            break;
        case PROP_NTH:
            priv->nth = g_value_get_int(value);
            break;
        case PROP_BLOCKING:
            priv->blocking = g_value_get_boolean(value);
            break;
        case PROP_NORMALIZE:
            priv->normalize = g_value_get_boolean(value);
            break;
        case PROP_ROI:
            priv->roi = g_value_get_boolean(value);
            break;
        case PROP_ROI_X:
            priv->roi_x = g_value_get_uint(value);
            break;
        case PROP_ROI_Y:
            priv->roi_y = g_value_get_uint(value);
            break;
        case PROP_ROI_WIDTH:
            priv->roi_width = g_value_get_uint(value);
            break;
        case PROP_ROI_HEIGHT:
            priv->roi_height = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void 
ufo_filter_reader_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterReaderPrivate *priv = UFO_FILTER_READER_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_PATH:
            g_value_set_string(value, priv->path);
            break;
        case PROP_COUNT:
            g_value_set_int(value, priv->count);
            break;
        case PROP_NTH:
            g_value_set_int(value, priv->nth);
            break;
        case PROP_BLOCKING:
            g_value_set_boolean(value, priv->blocking);
            break;
        case PROP_NORMALIZE:
            g_value_set_boolean(value, priv->normalize);
            break;
        case PROP_ROI:
            g_value_set_boolean(value, priv->roi);
            break;
        case PROP_ROI_X:
            g_value_set_uint(value, priv->roi_x);
            break;
        case PROP_ROI_Y:
            g_value_set_uint(value, priv->roi_y);
            break;
        case PROP_ROI_WIDTH:
            g_value_set_uint(value, priv->roi_width);
            break;
        case PROP_ROI_HEIGHT:
            g_value_set_uint(value, priv->roi_height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void 
ufo_filter_reader_finalize(GObject *object)
{
    UfoFilterReaderPrivate *priv = UFO_FILTER_READER_GET_PRIVATE(object);

    if (priv->path)
        g_free(priv->path);

    if (image_buffer != NULL)
        g_free(image_buffer);

    if (priv->frame_buffer)
        g_free(priv->frame_buffer);

    if (priv->filenames != NULL) {
        g_slist_foreach(priv->filenames, (GFunc) g_free, NULL);
        g_slist_free(priv->filenames);
    }

    G_OBJECT_CLASS(ufo_filter_reader_parent_class)->finalize(object);
}

static void 
ufo_filter_reader_class_init(UfoFilterReaderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterSourceClass *filter_class = UFO_FILTER_SOURCE_CLASS(klass);

    gobject_class->set_property = ufo_filter_reader_set_property;
    gobject_class->get_property = ufo_filter_reader_get_property;
    gobject_class->finalize = ufo_filter_reader_finalize;
    filter_class->source_initialize = ufo_filter_reader_initialize;
    filter_class->generate = ufo_filter_reader_generate_cpu;

    reader_properties[PROP_PATH] = 
        g_param_spec_string("path",
            "Glob-style pattern.",
            "Glob-style pattern that describes the file path.",
            "*.tif",
            G_PARAM_READWRITE);

    reader_properties[PROP_COUNT] =
        g_param_spec_int("count",
        "Number of files",
        "Number of files to read.",
        -1, G_MAXINT, -1,
        G_PARAM_READWRITE);

    reader_properties[PROP_NTH] =
        g_param_spec_int("nth",
        "Read from nth file",
        "Read from nth file.",
        -1, G_MAXINT, -1,
        G_PARAM_READWRITE);

    /**
     * UfoFilterReader:blocking:
     *
     * Block the reader and do not return unless #UfoFilterReader:count files
     * have been read. This is useful in case not all files are available the
     * time the reader was started.
     */
    reader_properties[PROP_BLOCKING] = 
        g_param_spec_boolean("blocking",
        "Block reader",
        "Block until all files are read.",
        FALSE,
        G_PARAM_READWRITE);

    reader_properties[PROP_NORMALIZE] = 
        g_param_spec_boolean("normalize",
        "Normalize values",
        "Whether 8-bit or 16-bit values are normalized to [0.0, 1.0]",
        FALSE,
        G_PARAM_READWRITE);

    reader_properties[PROP_ROI] = 
        g_param_spec_boolean("region-of-interest",
        "Read region of interest",
        "Read region of interest instead of full image",
        FALSE,
        G_PARAM_READWRITE);

    reader_properties[PROP_ROI_X] = 
        g_param_spec_uint("x",
            "Horizontal coordinate",
            "Horizontal coordinate from where to start the ROI",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    reader_properties[PROP_ROI_Y] = 
        g_param_spec_uint("y",
            "Vertical coordinate",
            "Vertical coordinate from where to start the ROI",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    reader_properties[PROP_ROI_WIDTH] = 
        g_param_spec_uint("width",
            "Width",
            "Width of the region of interest",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    reader_properties[PROP_ROI_HEIGHT] = 
        g_param_spec_uint("height",
            "Height",
            "Height of the region of interest",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property(gobject_class, i, reader_properties[i]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterReaderPrivate));
}

static void 
ufo_filter_reader_init(UfoFilterReader *self)
{
    UfoFilterReaderPrivate *priv = NULL;
    self->priv = priv = UFO_FILTER_READER_GET_PRIVATE(self);
    priv->path = g_strdup("*.tif");
    priv->count = -1;
    priv->nth = -1;
    priv->blocking = FALSE;
    priv->normalize = FALSE;
    priv->more_pages = FALSE;
    priv->roi = FALSE;
    priv->roi_x = priv->roi_y = priv->roi_width = priv->roi_height = 0;
    priv->frame_buffer = NULL;
    priv->current_tiff = NULL;

    ufo_filter_register_outputs (UFO_FILTER (self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new (UFO_TYPE_FILTER_READER, NULL);
}
