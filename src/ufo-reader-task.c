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

#include <gmodule.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
#include <glob.h>

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

    FILE *edf;
    TIFF *tiff;
    gboolean big_endian;
    guint32 width;
    guint32 height;
    guint16 bps;
    guint16 spp;
    gsize size;

    guint roi_y;
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
    PROP_ROI_Y,
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
read_tiff_data (UfoReaderTaskPrivate *priv, gpointer buffer, UfoRequisition *requisition)
{
    const guint32 width = requisition->dims[0];
    const guint32 height = requisition->dims[1];
    tsize_t result;
    int offset = 0;
    int step = width;

    if (priv->bps > 8) {
        if (priv->bps <= 16)
            step *= 2;
        else
            step *= 4;
    }

    for (guint32 i = 0; i < height; i++) {
        result = TIFFReadScanline (priv->tiff, ((gchar *) buffer) + offset, i + priv->roi_y, 0);

        if (result == -1)
            return FALSE;

        offset += step;
    }

    return TRUE;
}

static gboolean
is_tiff_file (const gchar *filename)
{
    return g_str_has_suffix (filename, ".tiff") ||
           g_str_has_suffix (filename, ".tif");
}

static gboolean
is_edf_file (const gchar *filename)
{
    return g_str_has_suffix (filename, ".edf");
}

static gboolean
has_valid_extension (const gchar *filename)
{
    return is_tiff_file (filename) || is_edf_file (filename);
}

static GSList *
read_filenames (UfoReaderTaskPrivate *priv)
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
            result = g_slist_append (result, g_strdup (filename));
        else
            g_warning ("Ignoring `%s'", filename);
    }

    globfree (&glob_vector);
    g_free (pattern);
    return result;
}

static void
ufo_reader_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoReaderTask *node;
    UfoReaderTaskPrivate *priv;
    gint n_files;
    gint partition;
    gint index;
    gint total;

    node = UFO_READER_TASK (task);
    priv = node->priv;

    priv->filenames = read_filenames (priv);

    if (priv->filenames == NULL) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "Path does not match any files");
        return;
    }

    ufo_task_node_get_partition (UFO_TASK_NODE (task), (guint*) &index, (guint*) &total);
    n_files = priv->count == -1 ? G_MAXINT : priv->count;
    partition = n_files / total;
    priv->current_count = index * partition;
    priv->count = (index + 1) * partition;
    priv->current_filename = g_slist_nth (priv->filenames, (guint) priv->current_count);
}

static void
read_edf_metadata (UfoReaderTaskPrivate *priv)
{
    gchar *header = g_malloc (1024);
    gchar **tokens;
    size_t num_bytes;

    num_bytes = fread (header, 1, 1024, priv->edf);

    if (num_bytes != 1024) {
        g_free (header);
        fclose (priv->edf);
        return;
    }

    tokens = g_strsplit(header, ";", 0);
    priv->big_endian = FALSE;

    for (guint i = 0; tokens[i] != NULL; i++) {
        gchar **key_value = g_strsplit (tokens[i], "=", 0);

        if (g_strcmp0 (g_strstrip (key_value[0]), "Dim_1") == 0)
            priv->width = (guint) atoi (key_value[1]);
        else if (g_strcmp0 (g_strstrip (key_value[0]), "Dim_2") == 0)
            priv->height = (guint) atoi (key_value[1]);
        else if (g_strcmp0 (g_strstrip (key_value[0]), "Size") == 0)
            priv->size = (guint) atoi (key_value[1]);
        else if ((g_strcmp0 (g_strstrip (key_value[0]), "ByteOrder") == 0) &&
                 (g_strcmp0 (g_strstrip (key_value[1]), "HighByteFirst") == 0))
            priv->big_endian = TRUE;

        g_strfreev (key_value);
    }

    g_strfreev(tokens);
    g_free(header);
    priv->bps = 32;
    priv->spp = 1;
}

static gboolean
read_edf_data (UfoReaderTaskPrivate *priv,
               gpointer buffer)
{
    gsize file_size;
    gsize num_bytes;
    gssize header_size;

    fseek (priv->edf, 0L, SEEK_END);
    file_size = (gsize) ftell (priv->edf);
    header_size = (gssize) (file_size - priv->size);
    fseek (priv->edf, header_size, SEEK_SET);

    num_bytes = fread (buffer, 1, priv->size, priv->edf);

    if (num_bytes != priv->size)
        return FALSE;

    if ((G_BYTE_ORDER == G_LITTLE_ENDIAN) && priv->big_endian) {
        guint32 *data = (guint32 *) buffer;
        guint n_pixels = priv->width * priv->height;

        for (guint i = 0; i < n_pixels; i++)
            data[i] = g_ntohl (data[i]);
    }

    return TRUE;
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

        if (is_tiff_file (name)) {
            priv->tiff = TIFFOpen (name, "r");

            TIFFGetField (priv->tiff, TIFFTAG_BITSPERSAMPLE, &priv->bps);
            TIFFGetField (priv->tiff, TIFFTAG_SAMPLESPERPIXEL, &priv->spp);
            TIFFGetField (priv->tiff, TIFFTAG_IMAGEWIDTH, &priv->width);
            TIFFGetField (priv->tiff, TIFFTAG_IMAGELENGTH, &priv->height);
        }
        else if (is_edf_file (name)) {
            priv->edf = fopen (name, "rb");
            read_edf_metadata (priv);
        }
    }

    requisition->n_dims = 2;

    if (priv->roi_height == 0 && priv->roi_y == 0) {
        requisition->dims[0] = priv->width;
        requisition->dims[1] = priv->height;
    }
    else {
        requisition->dims[0] = priv->width;

        if (priv->roi_height > 0)
            requisition->dims[1] = MIN (priv->height - priv->roi_y, priv->roi_height);
        else
            requisition->dims[1] = priv->height - priv->roi_y;
    }
}

static void
ufo_reader_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               UfoInputParam **in_params,
                               UfoTaskMode *mode)
{
    *n_inputs = 0;
    *mode = UFO_TASK_MODE_GENERATOR;
}

static gboolean
ufo_reader_task_generate (UfoCpuTask *task,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoReaderTaskPrivate *priv;
    UfoProfiler *profiler;

    priv = UFO_READER_TASK_GET_PRIVATE (UFO_READER_TASK (task));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    if ((priv->current_count < priv->count) && (priv->current_filename != NULL)) {
        gpointer data = ufo_buffer_get_host_array (output, NULL);

        ufo_profiler_start (profiler, UFO_PROFILER_TIMER_IO);

        if (priv->tiff != NULL) {
            read_tiff_data (priv, data, requisition);
            TIFFClose (priv->tiff);
            priv->tiff = NULL;
        }
        else if (priv->edf != NULL) {
            read_edf_data (priv, data);
            fclose (priv->edf);
            priv->edf = NULL;
        }

        ufo_profiler_stop (profiler, UFO_PROFILER_TIMER_IO);
        ufo_profiler_start (profiler, UFO_PROFILER_TIMER_CPU);

        if (priv->bps < 32) {
            UfoBufferDepth depth;

            depth = priv->bps <= 8 ? UFO_BUFFER_DEPTH_8U : UFO_BUFFER_DEPTH_16U;
            ufo_buffer_convert (output, depth);
        }

        ufo_profiler_stop (profiler, UFO_PROFILER_TIMER_CPU);

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
        case PROP_ROI_Y:
            priv->roi_y = g_value_get_uint (value);
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
        case PROP_ROI_Y:
            g_value_set_uint (value, priv->roi_y);
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

    g_free (priv->path);
    priv->path = NULL;

    if (priv->filenames != NULL) {
        g_slist_foreach (priv->filenames, (GFunc) g_free, NULL);
        g_slist_free (priv->filenames);
        priv->filenames = NULL;
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
    iface->generate = ufo_reader_task_generate;
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

    properties[PROP_ROI_Y] =
        g_param_spec_uint("y",
            "Vertical coordinate",
            "Vertical coordinate from where to start reading the image",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_ROI_HEIGHT] =
        g_param_spec_uint("height",
            "Height",
            "Height of the region of interest to read",
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
    priv->roi_y = 0;
    priv->roi_height = 0;
    priv->tiff = NULL;
    priv->edf = NULL;
}
