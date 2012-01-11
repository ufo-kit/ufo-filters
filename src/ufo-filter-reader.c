#include <gmodule.h>
#include <stdlib.h>
#include <tiffio.h>

#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include <ufo/ufo-resource-manager.h>

#include "ufo-filter-reader.h"


struct _UfoFilterReaderPrivate {
    gchar *path;
    gchar *prefix;
    gint count;
    gint nth;
    gboolean blocking;
};

GType ufo_filter_reader_get_type(void) G_GNUC_CONST;

/* Inherit from UFO_TYPE_FILTER */
G_DEFINE_TYPE(UfoFilterReader, ufo_filter_reader, UFO_TYPE_FILTER);

#define UFO_FILTER_READER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_READER, UfoFilterReaderPrivate))

enum {
    PROP_0,
    PROP_PATH,
    PROP_PREFIX,
    PROP_COUNT,
    PROP_BLOCKING,
    PROP_NTH,
    N_PROPERTIES
};

static GParamSpec *reader_properties[N_PROPERTIES] = { NULL, };


static gboolean filter_decode_tiff(TIFF *tif, void *buffer)
{
    const int strip_size = TIFFStripSize(tif);
    const int n_strips = TIFFNumberOfStrips(tif);
    int offset = 0;
    int result = 0;

    for (int strip = 0; strip < n_strips; strip++) {
        result = TIFFReadEncodedStrip(tif, strip, buffer+offset, strip_size);
        if (result == -1)
            return FALSE;
        offset += result;
    }
    return TRUE;
}

static void *filter_read_tiff(const gchar *filename, 
    guint16 *bits_per_sample,
    guint16 *samples_per_pixel,
    guint32 *width,
    guint32 *height)
{
    TIFF *tif = TIFFOpen(filename, "r");
    if (tif == NULL)
        return NULL;

    TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, bits_per_sample);
    TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, samples_per_pixel);
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, height);

    /* XXX: something creates files with 0 samples per pixel */
    if (*samples_per_pixel > 1) {
        g_warning("%s has %i samples per pixel (%i bps)", filename, *samples_per_pixel, *bits_per_sample);
        /* goto error_close; */
    }

    size_t bytes_per_sample = *bits_per_sample >> 3;
    void *buffer = g_malloc0(bytes_per_sample * (*width) * (*height));

    if (!filter_decode_tiff(tif, buffer))
        goto error_close;

    TIFFClose(tif);
    return buffer;

error_close:
    TIFFClose(tif);
    return NULL;
}

static void *filter_read_edf(const gchar *filename, 
    guint16 *bits_per_sample,
    guint16 *samples_per_pixel,
    guint32 *width, 
    guint32 *height)
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
    int w = 0, h = 0, size = 0;

    while (tokens[index] != NULL) {
        gchar **key_value = g_strsplit(tokens[index], "=", 0);
        if (g_strcmp0(g_strstrip(key_value[0]), "Dim_1") == 0)
            w = atoi(key_value[1]);
        else if (g_strcmp0(g_strstrip(key_value[0]), "Dim_2") == 0)
            h = atoi(key_value[1]);
        else if (g_strcmp0(g_strstrip(key_value[0]), "Size") == 0)
            size = atoi(key_value[1]);
        else if ((g_strcmp0(g_strstrip(key_value[0]), "ByteOrder") == 0) &&
                 (g_strcmp0(g_strstrip(key_value[1]), "HighByteFirst") == 0))
            big_endian = TRUE;
        g_strfreev(key_value);
        index++;
    }

    g_strfreev(tokens);
    g_free(header);

    if (w * h * sizeof(float) != size) {
        fclose(fp);
        return NULL;
    }

    *bits_per_sample = 32;
    *samples_per_pixel = 1;
    *width = w;
    *height = h;

    /* Skip header */
    fseek(fp, 0L, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, file_size - size, SEEK_SET);

    /* Read data */
    gchar *buffer = g_malloc0(size); 
    num_bytes = fread(buffer, 1, size, fp);
    fclose(fp);
    if (num_bytes != size) {
        g_free(buffer);
        return NULL;
    }

    if ((G_BYTE_ORDER == G_LITTLE_ENDIAN) && big_endian) {
        guint32 *data = (guint32 *) buffer;    
        for (int i = 0; i < w*h; i++)
            data[i] = g_ntohl(data[i]);
    }
    return buffer;
}

static void filter_dispose_filenames(GList *filenames)
{
    if (filenames != NULL) {
        g_list_foreach(filenames, (GFunc) g_free, NULL);
        filenames = NULL;
    }
}

static GList *filter_read_filenames(UfoFilterReaderPrivate *priv)
{
    GDir *directory = g_dir_open(priv->path, 0, NULL);
    if (directory == NULL) {
        g_debug("Could not open %s", priv->path);
        return NULL;
    }

    GList *filenames = NULL;
    gchar *filename = (gchar *) g_dir_read_name(directory);
    while (filename != NULL) {
        if (((priv->prefix == NULL) || (g_str_has_prefix(filename, priv->prefix))) &&
            (g_str_has_suffix(filename, "tif") || g_str_has_suffix(filename, "edf"))) {
            filenames = g_list_append(filenames, g_strdup_printf("%s/%s", priv->path, filename));
        }
        filename = (gchar *) g_dir_read_name(directory);
    }
    g_dir_close(directory);
    return g_list_sort(filenames, (GCompareFunc) g_strcmp0);
}

static void ufo_filter_reader_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterReaderPrivate *priv = UFO_FILTER_READER_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_PATH:
            g_free(priv->path);
            priv->path = g_strdup(g_value_get_string(value));
            break;
        case PROP_PREFIX:
            g_free(priv->prefix);
            priv->prefix = g_strdup(g_value_get_string(value));
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
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_reader_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterReaderPrivate *priv = UFO_FILTER_READER_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_PATH:
            g_value_set_string(value, priv->path);
            break;
        case PROP_PREFIX:
            g_value_set_string(value, priv->prefix);
            break;
        case PROP_COUNT:
            g_value_set_int(value, priv->count);
            break;
        case PROP_NTH:
            g_value_set_int(value, priv->nth);
            break;
        case PROP_BLOCKING:
            g_value_set_boolean(value, priv->blocking);
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_reader_process(UfoFilter *self)
{
    g_return_if_fail(UFO_IS_FILTER(self));

    UfoFilterReaderPrivate *priv = UFO_FILTER_READER_GET_PRIVATE(self);
    UfoChannel *output_channel = ufo_filter_get_output_channel(self);
    
    GList *filenames = filter_read_filenames(priv);
    const guint max_count = (priv->count == -1) ? G_MAXUINT : priv->count;
    guint32 width, height;
    guint16 bits_per_sample, samples_per_pixel;

    GList *filename = NULL;
    if ((priv->nth > -1) && (priv->nth < g_list_length(filenames)))
        filename = g_list_nth(filenames, priv->nth);
    else
        filename = g_list_first(filenames);

    guint i = 0;
    gboolean buffers_initialized = FALSE;
    UfoBuffer *output_buffer = NULL;

    while (i < max_count) {
        if (filename == NULL) {
            if (priv->blocking) {
                /* If file could not be opened and we block, sleep for 1000ms and
                 * readout directory again */
                g_usleep(1000 * 1000);
                filter_dispose_filenames(filenames);
                filenames = filter_read_filenames(priv);        
                if ((priv->nth > -1) && (priv->nth < g_list_length(filenames)))
                    filename = g_list_nth(filenames, priv->nth + i);
                else
                    filename = g_list_nth(filenames, i);
                continue;
            }
            else
                break;
        }

        void *buffer;
        if (g_str_has_suffix(filename->data, "tif"))
            buffer = filter_read_tiff((char *) filename->data,
                &bits_per_sample, &samples_per_pixel,
                &width, &height);
        else
            buffer = filter_read_edf((char *) filename->data,
                &bits_per_sample, &samples_per_pixel,
                &width, &height);

        /* break out of the loop and insert finishing buffer if file is not valid */
        if (buffer == NULL)
            break;

        if (!buffers_initialized) {
            gint32 dimensions[2] = { width, height };
            ufo_channel_allocate_output_buffers(output_channel, 2, dimensions);
            buffers_initialized = TRUE;
        }

        output_buffer = ufo_channel_get_output_buffer(output_channel);

        const guint16 bytes_per_sample = bits_per_sample >> 3;
        ufo_buffer_set_host_array(output_buffer, buffer, bytes_per_sample * width * height, NULL);
        if (bits_per_sample < 32)
            ufo_buffer_reinterpret(output_buffer, bits_per_sample, width * height);

        ufo_channel_finalize_output_buffer(output_channel, output_buffer);
        g_free(buffer);
        filename = g_list_next(filename);
        i++;
    }

    /* No more data */
    ufo_channel_finish(output_channel);
    filter_dispose_filenames(filenames);
}

static void ufo_filter_reader_class_init(UfoFilterReaderClass *klass)
{
    /* override methods */
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_reader_set_property;
    gobject_class->get_property = ufo_filter_reader_get_property;
    filter_class->process = ufo_filter_reader_process;

    /* install properties */
    reader_properties[PROP_PREFIX] = 
        g_param_spec_string("prefix",
            "Filename prefix",
            "Prefix of input filename",
            "",
            G_PARAM_READWRITE);

    reader_properties[PROP_PATH] = 
        g_param_spec_string("path",
            "File path",
            "Path to data files",
            ".",
            G_PARAM_READWRITE);

    reader_properties[PROP_COUNT] =
        g_param_spec_int("count",
        "Number of files",
        "Number of files to read with -1 denoting all",
        -1,     /* minimum */
        8192,   /* maximum */
        -1,     /* default */
        G_PARAM_READWRITE);

    reader_properties[PROP_NTH] =
        g_param_spec_int("nth",
        "Start from nth file",
        "Start from nth file or first if -1",
        -1,     /* minimum */
        8192,   /* maximum */
        -1,     /* default */
        G_PARAM_READWRITE);

    reader_properties[PROP_BLOCKING] = 
        g_param_spec_boolean("blocking",
        "Block until all <count> files are read",
        "Block until all <count> files are read",
        FALSE,
        G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_PATH, reader_properties[PROP_PATH]);
    g_object_class_install_property(gobject_class, PROP_PREFIX, reader_properties[PROP_PREFIX]);
    g_object_class_install_property(gobject_class, PROP_COUNT, reader_properties[PROP_COUNT]);
    g_object_class_install_property(gobject_class, PROP_NTH, reader_properties[PROP_NTH]);
    g_object_class_install_property(gobject_class, PROP_BLOCKING, reader_properties[PROP_BLOCKING]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterReaderPrivate));
}

static void ufo_filter_reader_init(UfoFilterReader *self)
{
    self->priv = UFO_FILTER_READER_GET_PRIVATE(self);
    self->priv->path = g_strdup(".");
    self->priv->prefix = NULL;
    self->priv->count = -1;
    self->priv->nth = -1;
    self->priv->blocking = FALSE;
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_READER, NULL);
}
