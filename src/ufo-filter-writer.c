#include <gmodule.h>
#include <tiffio.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include <ufo/ufo-resource-manager.h>

#include "ufo-filter-writer.h"

/**
 * SECTION:ufo-filter-writer
 * @Short_description: Write TIFF files
 * @Title: writer
 *
 * The writer node writes each incoming image as a TIFF using libtiff to disk.
 * Each file is prefixed with #UfoFilterWriter:prefix and written into
 * #UfoFilterWriter:path.
 */

struct _UfoFilterWriterPrivate {
    gchar *path;
    gchar *prefix;
    guint counter;
};

G_DEFINE_TYPE(UfoFilterWriter, ufo_filter_writer, UFO_TYPE_FILTER_SINK)

#define UFO_FILTER_WRITER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_WRITER, UfoFilterWriterPrivate))

enum {
    PROP_0,
    PROP_PATH,
    PROP_PREFIX,
    N_PROPERTIES
};

static GParamSpec *reader_properties[N_PROPERTIES] = { NULL, };


static gboolean 
filter_write_tiff(float *buffer, const gchar *name, guint width, guint height)
{
    gboolean success = TRUE;
    TIFF *tif = TIFFOpen(name, "w");

    if (tif == NULL)
        return FALSE;

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 32);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

    const guint32 rows_per_strip = TIFFDefaultStripSize(tif, (guint32)-1);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, rows_per_strip);

    for (guint y = 0; y < height; y++, buffer += width)
        TIFFWriteScanline(tif, buffer, y, 0);

    TIFFClose(tif);
    return success;
}

static GError *
ufo_filter_writer_process_cpu (UfoFilterSink *self, UfoBuffer *params[], gpointer cmd_queue)
{
    UfoFilterWriterPrivate *priv = UFO_FILTER_WRITER_GET_PRIVATE(self);
    guint width, height;
    ufo_buffer_get_2d_dimensions(params[0], &width, &height);

    float *data = ufo_buffer_get_host_array(params[0], (cl_command_queue) cmd_queue);
    gchar *filename = g_strdup_printf("%s/%s%05i.tif", priv->path, priv->prefix, priv->counter++);

    if (!filter_write_tiff(data, filename, width, height))
        /* TODO: create proper error */
        g_message("something went wrong");

    g_free(filename);
    return NULL;
}

static void 
ufo_filter_writer_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterWriter *filter = UFO_FILTER_WRITER(object);

    switch (property_id) {
        case PROP_PATH:
            g_free(filter->priv->path);
            filter->priv->path = g_strdup(g_value_get_string(value));
            break;
        case PROP_PREFIX:
            g_free(filter->priv->prefix);
            filter->priv->prefix = g_strdup(g_value_get_string(value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void 
ufo_filter_writer_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterWriter *filter = UFO_FILTER_WRITER(object);

    switch (property_id) {
        case PROP_PATH:
            g_value_set_string(value, filter->priv->path);
            break;
        case PROP_PREFIX:
            g_value_set_string(value, filter->priv->prefix);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void 
ufo_filter_writer_class_init(UfoFilterWriterClass *klass)
{
    UfoFilterSinkClass *filter_class = UFO_FILTER_SINK_CLASS (klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_filter_writer_set_property;
    gobject_class->get_property = ufo_filter_writer_get_property;
    filter_class->consume = ufo_filter_writer_process_cpu;

    /**
     * UfoFilterWriter:prefix:
     *
     * Specifies the prefix that is prepended to each written file. Currently,
     * the filename is made up according to the format string ("%s%05i.tif" %
     * (prefix, current image number)).
     */
    reader_properties[PROP_PREFIX] = 
        g_param_spec_string("prefix",
            "Filename prefix",
            "Prefix of output filename.",
            "",
            G_PARAM_READWRITE);

    reader_properties[PROP_PATH] = 
        g_param_spec_string("path",
            "File path",
            "Path where to store files.",
            ".",
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_PATH, reader_properties[PROP_PATH]);
    g_object_class_install_property(gobject_class, PROP_PREFIX, reader_properties[PROP_PREFIX]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterWriterPrivate));
}

static void 
ufo_filter_writer_init(UfoFilterWriter *self)
{
    self->priv = UFO_FILTER_WRITER_GET_PRIVATE(self);
    self->priv->path = g_strdup(".");
    self->priv->prefix = NULL;
    self->priv->counter = 0;

    ufo_filter_register_inputs (UFO_FILTER (self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_WRITER, NULL);
}

