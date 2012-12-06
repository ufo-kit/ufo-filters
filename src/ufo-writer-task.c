/**
 * SECTION:ufo-writer-task
 * @Short_description: Write TIFF files
 * @Title: writer
 *
 * The writer node writes each incoming image as a TIFF using libtiff to disk.
 * Each file is prefixed with #UfoWriterTask:prefix and written into
 * #UfoWriterTask:path.
 */

#include <glib-object.h>
#include <gmodule.h>
#include <tiffio.h>
#include <ufo-cpu-task-iface.h>
#include "ufo-writer-task.h"

struct _UfoWriterTaskPrivate {
    gchar *path;
    gchar *prefix;
    guint counter;
    gsize width;
    gsize height;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoWriterTask, ufo_writer_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_WRITER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_WRITER_TASK, UfoWriterTaskPrivate))

enum {
    PROP_0,
    PROP_PATH,
    PROP_PREFIX,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_writer_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_WRITER_TASK, NULL));
}

static gboolean
write_tiff_data (gfloat *data, const gchar *name, gsize width, gsize height)
{
    guint32 rows_per_strip;
    gboolean success = TRUE;

    TIFF *tif = TIFFOpen(name, "w");

    if (tif == NULL)
        return FALSE;

    rows_per_strip = TIFFDefaultStripSize(tif, (guint32)-1);

    TIFFSetField (tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField (tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE, 32);
    TIFFSetField (tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
    TIFFSetField (tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField (tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (tif, TIFFTAG_ROWSPERSTRIP, rows_per_strip);

    for (guint y = 0; y < height; y++, data += width)
        TIFFWriteScanline(tif, data, y, 0);

    TIFFClose(tif);
    return success;
}

static void
ufo_writer_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_writer_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoWriterTaskPrivate *priv;
    UfoRequisition buffer_req;
    
    priv = UFO_WRITER_TASK_GET_PRIVATE (UFO_WRITER_TASK (task));
    ufo_buffer_get_requisition (inputs[0], &buffer_req);
    priv->width = buffer_req.dims[0];
    priv->height = buffer_req.dims[1];
    requisition->n_dims = 0;
}

static void
ufo_writer_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               guint **n_dims,
                               UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 1;
    *n_dims = g_new0 (guint, 1);
    (*n_dims)[0] = 2;
}

static gboolean
ufo_writer_task_process (UfoCpuTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoWriterTaskPrivate *priv;
    gchar *filename;
    gpointer data;
    
    priv = UFO_WRITER_TASK_GET_PRIVATE (UFO_WRITER_TASK (task));
    filename = g_strdup_printf("%s/%s%05i.tif",
                               priv->path,
                               priv->prefix,
                               priv->counter++);

    data = ufo_buffer_get_host_array (inputs[0], NULL);
    write_tiff_data (data, filename, priv->width, priv->height);
    g_free (filename);

    return TRUE;
}

static void
ufo_writer_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoWriterTaskPrivate *priv = UFO_WRITER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_PATH:
            g_free (priv->path);
            priv->path = g_value_dup_string (value);
            break;
        case PROP_PREFIX:
            g_free(priv->prefix);
            priv->prefix = g_value_dup_string (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_writer_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoWriterTaskPrivate *priv = UFO_WRITER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_PATH:
            g_value_set_string(value, priv->path);
            break;
        case PROP_PREFIX:
            g_value_set_string(value, priv->prefix);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_writer_task_setup;
    iface->get_structure = ufo_writer_task_get_structure;
    iface->get_requisition = ufo_writer_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_writer_task_process;
}

static void
ufo_writer_task_class_init (UfoWriterTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_writer_task_set_property;
    gobject_class->get_property = ufo_writer_task_get_property;

    /**
     * UfoWriterTask:prefix:
     *
     * Specifies the prefix that is prepended to each written file. Currently,
     * the filename is made up according to the format string ("%s%05i.tif" %
     * (prefix, current image number)).
     */
    properties[PROP_PREFIX] =
        g_param_spec_string ("prefix",
            "Filename prefix",
            "Prefix of output filename.",
            "",
            G_PARAM_READWRITE);

    properties[PROP_PATH] =
        g_param_spec_string ("path",
            "File path",
            "Path where to store files.",
            ".",
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoWriterTaskPrivate));
}

static void
ufo_writer_task_init(UfoWriterTask *self)
{
    self->priv = UFO_WRITER_TASK_GET_PRIVATE(self);
    self->priv->path = g_strdup (".");
    self->priv->prefix = NULL;
    self->priv->counter = 0;
}
