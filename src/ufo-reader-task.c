/**
 * SECTION:ufo-reader-task
 * @Short_description: Read TIFF and EDF files
 * @Title: reader
 *
 * The reader node loads single files from disk and provides them as a stream
 * The nominal resolution can be decreased by specifying the #UfoReaderTask:x
 * and #UfoReaderTask:y coordinates, and the #UfoReaderTask:width and
 * #UfoReaderTask:height of a region of interest.
 */

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

#include <ufo-cpu-task-iface.h>
#include "ufo-reader-task.h"

struct _UfoReaderTaskPrivate {
    gchar *path;
    gint count;
    gint current_count;
    gint nth;
    gboolean blocking;
    gboolean normalize;
    gboolean more_pages;
    GSList *filenames;
    GSList *current_filename;

    TIFF *tiff;
    guint32 width;
    guint32 height;
    guint16 bps;
    guint16 spp;

    gboolean roi;
    guint roi_x;
    guint roi_y;
    guint roi_width;
    guint roi_height;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoReaderTask, ufo_reader_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_READER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_READER_TASK, UfoReaderTaskPrivate))

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

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_reader_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_READER_TASK, NULL));
}

static gboolean
read_tiff_data (TIFF *tif, gpointer buffer)
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
/* static gboolean */
/* read_tiff (TIFF *tif, */
/*            gpointer buffer) */
/* { */
/*     /1* guint16 bits_per_sample = 8; *1/ */
/*     /1* TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample); *1/ */
/*     /1* TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, samples_per_pixel); *1/ */
/*     /1* TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, width); *1/ */
/*     /1* TIFFGetField(tif, TIFFTAG_IMAGELENGTH, height); *1/ */

/*     /1* if (*samples_per_pixel > 1) *1/ */
/*     /1*     g_warning("TIFF has %i samples per pixel", *samples_per_pixel); *1/ */

/*     /1* *bytes_per_sample = bits_per_sample >> 3; *1/ */
/*     /1* gsize size = *bytes_per_sample * (*width) * (*height); *1/ */

/*     /1* if ((*buffer == NULL) || (image_size != size)) { *1/ */
/*     /1*     *buffer = g_malloc0(size); *1/ */
/*     /1*     image_size = size; *1/ */
/*     /1* } *1/ */

/*     if (!filter_decode_tiff(tif, *buffer)) */
/*         goto error_close; */

/*     return TIFFReadDirectory(tif) == 1; */

/* error_close: */
/*     *buffer = NULL; */
/*     return FALSE; */
/* } */

static gboolean
has_valid_extension (const gchar *filename)
{
    return g_str_has_suffix (filename, ".tiff") || g_str_has_suffix (filename, ".tif");
}

static GSList *
read_filenames(UfoReaderTaskPrivate *priv)
{
    GSList *result = NULL;
    gchar *pattern;
    glob_t glob_vector;
    guint i = (priv->nth < 0) ? 0 : (guint) priv->nth;

    if (!has_valid_extension (priv->path) && (g_strrstr (priv->path, "*") == NULL))
        pattern = g_build_filename (priv->path, "*", NULL);
    else
        pattern = g_strdup (priv->path);

    glob (pattern, GLOB_MARK | GLOB_TILDE, NULL, &glob_vector);

    for (; i < glob_vector.gl_pathc; i++) {
        const gchar *filename = glob_vector.gl_pathv[i];

        if (has_valid_extension (filename))
            result = g_slist_append(result, g_strdup(filename));
        else
            g_warning ("Ignoring `%s'", filename);
    }

    globfree(&glob_vector);
    g_free (pattern);
    return result;
}

/* static void */
/* push_data(UfoReaderTaskPrivate *priv, gpointer, guint src_width, guint src_height, guint bytes_per_sample) */
/* { */
/*     if (!priv->roi) { */
/*         ufo_buffer_set_host_array(output, priv->frame_buffer, bytes_per_sample * src_width * src_height, NULL); */

/*         if (bytes_per_sample < 4) */
/*             ufo_buffer_reinterpret(output, bytes_per_sample << 3, src_width * src_height, priv->normalize); */
/*     } */
/*     else { */
/*         guint x1 = priv->roi_x, y1 = priv->roi_y; */
/*         guint x2 = x1 + priv->roi_width, y2 = y1 + priv->roi_height; */

/*         /1* Don't do anything if we are completely out of bounds *1/ */
/*         if (x1 <= src_width && y1 <= src_height) { */

/*             guint rd_width = x2 > src_width ? src_width - x1 : priv->roi_width; */
/*             guint rd_height = y2 > src_height ? src_height - y1 : priv->roi_height; */
/*             gfloat *in_data = (gfloat *) priv->frame_buffer; */
/*             gfloat *out_data = ufo_buffer_get_host_array(output, NULL); */

/*             if (rd_width == src_width) { */
/*                 g_memmove(out_data, in_data + y1*src_width, */
/*                         rd_width * rd_height * sizeof(gfloat)); */
/*             } */
/*             else { */
/*                 for (guint y = 0; y < rd_height; y++) { */
/*                     g_memmove(out_data + y*priv->roi_width, in_data + (y + y1)*src_width + x1, */
/*                             rd_width * sizeof(gfloat)); */
/*                 } */
/*             } */

/*             if (bytes_per_sample < 4) */
/*                 g_warning("Region of interest with non-float data is not yet supported!"); */
/*         } */
/*     } */
/* } */

static void
ufo_reader_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoReaderTask *node;
    UfoReaderTaskPrivate *priv;
    
    node = UFO_READER_TASK (task);
    priv = node->priv;

    priv->filenames = read_filenames (priv);

    if (priv->filenames == NULL) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "Path does not match any files");
        return;
    }

    priv->current_filename = priv->filenames;
    priv->current_count = 0;
    priv->count = priv->count == -1 ? G_MAXINT : priv->count;
}

static void
ufo_reader_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoReaderTaskPrivate *priv;
        
    priv = UFO_READER_TASK_GET_PRIVATE (UFO_READER_TASK (task));

    if ((priv->current_count < priv->count) && (priv->current_filename != NULL)) {
        const gchar *name = (gchar *) priv->current_filename->data;

        priv->tiff = TIFFOpen(name, "r");

        TIFFGetField (priv->tiff, TIFFTAG_BITSPERSAMPLE, &priv->bps);
        TIFFGetField (priv->tiff, TIFFTAG_SAMPLESPERPIXEL, &priv->spp);
        TIFFGetField (priv->tiff, TIFFTAG_IMAGEWIDTH, &priv->width);
        TIFFGetField (priv->tiff, TIFFTAG_IMAGELENGTH, &priv->height);
    }

    requisition->n_dims = 2;

    if (!priv->roi || (priv->roi_width == 0) || (priv->roi_height == 0)) {
        requisition->dims[0] = priv->width;
        requisition->dims[1] = priv->height;
    }
    else {
        requisition->dims[0] = priv->roi_width;
        requisition->dims[1] = priv->roi_height;
    }
}

static void
ufo_reader_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               UfoInputParameter **in_params)
{
    *n_inputs = 0;
}

static gboolean
ufo_reader_task_process (UfoCpuTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoReaderTaskPrivate *priv;
    
    priv = UFO_READER_TASK_GET_PRIVATE (UFO_READER_TASK (task));

    if ((priv->current_count < priv->count) && (priv->current_filename != NULL)) {
        gpointer data;

        data = ufo_buffer_get_host_array (output, NULL);
        read_tiff_data (priv->tiff, data);
        TIFFClose (priv->tiff); 

        priv->current_filename = g_slist_next(priv->current_filename);
        priv->current_count++;
        return TRUE;
    }

    return FALSE;
}

static void
ufo_reader_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoReaderTaskPrivate *priv = UFO_READER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_PATH:
            g_free (priv->path);
            priv->path = g_value_dup_string (value);
            break;
        case PROP_COUNT:
            priv->count = g_value_get_int (value);
            break;
        case PROP_NTH:
            priv->nth = g_value_get_int (value);
            break;
        case PROP_BLOCKING:
            priv->blocking = g_value_get_boolean (value);
            break;
        case PROP_NORMALIZE:
            priv->normalize = g_value_get_boolean (value);
            break;
        case PROP_ROI:
            priv->roi = g_value_get_boolean (value);
            break;
        case PROP_ROI_X:
            priv->roi_x = g_value_get_uint (value);
            break;
        case PROP_ROI_Y:
            priv->roi_y = g_value_get_uint (value);
            break;
        case PROP_ROI_WIDTH:
            priv->roi_width = g_value_get_uint (value);
            break;
        case PROP_ROI_HEIGHT:
            priv->roi_height = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_reader_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoReaderTaskPrivate *priv = UFO_READER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_PATH:
            g_value_set_string (value, priv->path);
            break;
        case PROP_COUNT:
            g_value_set_int (value, priv->count);
            break;
        case PROP_NTH:
            g_value_set_int (value, priv->nth);
            break;
        case PROP_BLOCKING:
            g_value_set_boolean (value, priv->blocking);
            break;
        case PROP_NORMALIZE:
            g_value_set_boolean (value, priv->normalize);
            break;
        case PROP_ROI:
            g_value_set_boolean (value, priv->roi);
            break;
        case PROP_ROI_X:
            g_value_set_uint (value, priv->roi_x);
            break;
        case PROP_ROI_Y:
            g_value_set_uint (value, priv->roi_y);
            break;
        case PROP_ROI_WIDTH:
            g_value_set_uint (value, priv->roi_width);
            break;
        case PROP_ROI_HEIGHT:
            g_value_set_uint (value, priv->roi_height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_reader_task_finalize (GObject *object)
{
    UfoReaderTaskPrivate *priv = UFO_READER_TASK_GET_PRIVATE (object);

    if (priv->path)
        g_free (priv->path);

    if (priv->filenames != NULL) {
        g_slist_foreach (priv->filenames, (GFunc) g_free, NULL);
        g_slist_free (priv->filenames);
    }

    G_OBJECT_CLASS (ufo_reader_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_reader_task_setup;
    iface->get_structure = ufo_reader_task_get_structure;
    iface->get_requisition = ufo_reader_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_reader_task_process;
}

static void
ufo_reader_task_class_init(UfoReaderTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_reader_task_set_property;
    gobject_class->get_property = ufo_reader_task_get_property;
    gobject_class->finalize = ufo_reader_task_finalize;

    properties[PROP_PATH] =
        g_param_spec_string("path",
            "Glob-style pattern.",
            "Glob-style pattern that describes the file path.",
            "*.tif",
            G_PARAM_READWRITE);

    properties[PROP_COUNT] =
        g_param_spec_int("count",
        "Number of files",
        "Number of files to read.",
        -1, G_MAXINT, -1,
        G_PARAM_READWRITE);

    properties[PROP_NTH] =
        g_param_spec_int("nth",
        "Read from nth file",
        "Read from nth file.",
        -1, G_MAXINT, -1,
        G_PARAM_READWRITE);

    /**
     * UfoReaderTask:blocking:
     *
     * Block the reader and do not return unless #UfoReaderTask:count files
     * have been read. This is useful in case not all files are available the
     * time the reader was started.
     */
    properties[PROP_BLOCKING] =
        g_param_spec_boolean("blocking",
        "Block reader",
        "Block until all files are read.",
        FALSE,
        G_PARAM_READWRITE);

    properties[PROP_NORMALIZE] =
        g_param_spec_boolean("normalize",
        "Normalize values",
        "Whether 8-bit or 16-bit values are normalized to [0.0, 1.0]",
        FALSE,
        G_PARAM_READWRITE);

    properties[PROP_ROI] =
        g_param_spec_boolean("region-of-interest",
        "Read region of interest",
        "Read region of interest instead of full image",
        FALSE,
        G_PARAM_READWRITE);

    properties[PROP_ROI_X] =
        g_param_spec_uint("x",
            "Horizontal coordinate",
            "Horizontal coordinate from where to start the ROI",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_ROI_Y] =
        g_param_spec_uint("y",
            "Vertical coordinate",
            "Vertical coordinate from where to start the ROI",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_ROI_WIDTH] =
        g_param_spec_uint("width",
            "Width",
            "Width of the region of interest",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_ROI_HEIGHT] =
        g_param_spec_uint("height",
            "Height",
            "Height of the region of interest",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoReaderTaskPrivate));
}

static void
ufo_reader_task_init(UfoReaderTask *self)
{
    UfoReaderTaskPrivate *priv = NULL;

    self->priv = priv = UFO_READER_TASK_GET_PRIVATE (self);
    priv->path = g_strdup ("*.tif");
    priv->count = -1;
    priv->nth = -1;
    priv->blocking = FALSE;
    priv->normalize = FALSE;
    priv->more_pages = FALSE;
    priv->roi = FALSE;
    priv->roi_x = priv->roi_y = priv->roi_width = priv->roi_height = 0;
}
