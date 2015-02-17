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
#include <tiffio.h>
#include <errno.h>

#include "ufo-write-task.h"

typedef enum {
    BITS_8U = 8,
    BITS_16U = 16,
    BITS_32FP = 32,
} BitDepth;

struct _UfoWriteTaskPrivate {
    gchar *format;
    gchar *template;
    guint counter;
    gboolean append;
    gsize width;
    gsize height;
    BitDepth depth;

    gboolean single;
    TIFF *tif;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoWriteTask, ufo_write_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_WRITE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_WRITE_TASK, UfoWriteTaskPrivate))

enum {
    PROP_0,
    PROP_FORMAT,
    PROP_SINGLE_FILE,
    PROP_APPEND,
    PROP_BITS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_write_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_WRITE_TASK, NULL));
}

static void
write_8bit_data (TIFF *tif, gfloat *data, gfloat min, gfloat max, guint width, guint height)
{
    guint8 *scanline;
    const gfloat range = max - min;

    TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE, 8);
    scanline = g_malloc (width);

    for (guint y = 0; y < height; y++, data += width) {
        for (guint i = 0; i < width; i++)
            scanline[i] = (guint8) ((data[i] - min) / range * 255);

        TIFFWriteScanline (tif, scanline, y, 0);
    }

    g_free (scanline);
}

static void
write_16bit_data (TIFF *tif, gfloat *data, gfloat min, gfloat max, guint width, guint height)
{
    guint16 *scanline;
    const gfloat range = max - min;

    TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE, 16);
    scanline = g_malloc (width * 2);

    for (guint y = 0; y < height; y++, data += width) {
        for (guint i = 0; i < width; i++)
            scanline[i] = (guint16) ((data[i] - min) / range * 65535);

        TIFFWriteScanline (tif, scanline, y, 0);
    }

    g_free (scanline);
}

static gboolean
write_tiff_data (UfoWriteTaskPrivate *priv, UfoBuffer *buffer)
{
    UfoRequisition requisition;
    guint32 rows_per_strip;
    gboolean success = TRUE;
    gpointer data;
    guint n_pages;
    guint width, height;

    ufo_buffer_get_requisition (buffer, &requisition);

    /* With a 3-dimensional input buffer, we create z-depth TIFF pages. */
    n_pages = requisition.n_dims == 3 ? (guint) requisition.dims[2] : 1;

    width = (guint) requisition.dims[0];
    height = (guint) requisition.dims[1];
    data = ufo_buffer_get_host_array (buffer, NULL);

    rows_per_strip = TIFFDefaultStripSize (priv->tif, (guint32) - 1);

    if (n_pages > 1)
        TIFFSetField (priv->tif, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);

    for (guint i = 0; i < n_pages; i++) {
        gfloat *start;

        TIFFSetField (priv->tif, TIFFTAG_IMAGEWIDTH, width);
        TIFFSetField (priv->tif, TIFFTAG_IMAGELENGTH, height);
        TIFFSetField (priv->tif, TIFFTAG_SAMPLESPERPIXEL, 1);
        TIFFSetField (priv->tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField (priv->tif, TIFFTAG_ROWSPERSTRIP, rows_per_strip);
        TIFFSetField (priv->tif, TIFFTAG_PAGENUMBER, i, n_pages);

        start = ((gfloat *) data) + i * width * height;

        if (priv->depth == BITS_32FP) {
            TIFFSetField (priv->tif, TIFFTAG_BITSPERSAMPLE, 32);
            TIFFSetField (priv->tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);

            for (guint y = 0; y < height; y++, start += width)
                TIFFWriteScanline (priv->tif, start, y, 0);

        }
        else {
            gfloat min, max;

            min = ufo_buffer_min (buffer, NULL);
            max = ufo_buffer_max (buffer, NULL);

            TIFFSetField (priv->tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);

            if (priv->depth == BITS_8U)
                write_8bit_data (priv->tif, start, min, max, width, height);
            else
                write_16bit_data (priv->tif, start, min, max, width, height);
        }

        TIFFWriteDirectory (priv->tif);
    }

    return success;
}

static gchar *
build_filename (UfoWriteTaskPrivate *priv)
{
    gchar *filename;

    if (priv->single)
        filename = g_strdup (priv->format);
    else
        filename = g_strdup_printf (priv->template, priv->counter);

    return filename;
}

static void
find_free_index (UfoWriteTaskPrivate *priv)
{
    while (g_file_test(build_filename(priv), G_FILE_TEST_EXISTS)) {
        priv->counter++;
    }
}

static gchar *
build_template (const gchar *format)
{
    gchar *template;
    gchar *percent;

    template = g_strdup (format);
    percent = g_strstr_len (template, -1, "%");

    if (percent != NULL) {
        percent++;

        while (*percent) {
            if (*percent == '%')
                *percent = '_';
            percent++;
        }
    }
    else {
        g_warning ("Specifier %%i not found. Appending it.");
        g_free (template);
        template = g_strconcat (format, "%i", NULL);
    }

    return template;
}

static void
open_tiff_file (UfoWriteTaskPrivate *priv)
{
    gchar *filename;
    const gchar *mode = priv->append ? "a" : "w";

    filename = build_filename (priv);
    priv->tif = TIFFOpen (filename, mode);
    g_free (filename);
}

static void
ufo_write_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoWriteTaskPrivate *priv;
    guint index;
    guint total;

    priv = UFO_WRITE_TASK_GET_PRIVATE (task);
    ufo_task_node_get_partition (UFO_TASK_NODE (task), &index, &total);
    priv->counter = index * 1000;

    if (priv->single) {
        open_tiff_file (priv);
    }
    else {
        gchar *dirname;

        if (priv->template)
            g_free (priv->template);

        priv->template = build_template (priv->format);
        dirname = g_path_get_dirname (priv->template);

        if (g_strcmp0 (dirname, ".")) {
            if (g_mkdir_with_parents (dirname, 0755)) {
                g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                             "Could not create directory `%s'", dirname);
            }
        }

        if (priv->append) {
            find_free_index(priv);
        }

        g_free (dirname);
    }
}

static void
ufo_write_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    requisition->n_dims = 0;
}

static guint
ufo_write_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_write_task_get_num_dimensions (UfoTask *task,
                               guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_write_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_write_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoWriteTaskPrivate *priv;
    UfoProfiler *profiler;

    priv = UFO_WRITE_TASK_GET_PRIVATE (UFO_WRITE_TASK (task));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    ufo_profiler_start (profiler, UFO_PROFILER_TIMER_IO);

    if (!priv->single)
        open_tiff_file (priv);

    if (!write_tiff_data (priv, inputs[0]))
        return FALSE;

    if (!priv->single)
        TIFFClose (priv->tif);

    ufo_profiler_stop (profiler, UFO_PROFILER_TIMER_IO);

    priv->counter++;
    return TRUE;
}

static void
ufo_write_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoWriteTaskPrivate *priv = UFO_WRITE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FORMAT:
            g_free (priv->format);
            priv->format = g_value_dup_string (value);
            break;
        case PROP_SINGLE_FILE:
            priv->single = g_value_get_boolean (value);
            break;
        case PROP_APPEND:
            priv->append = g_value_get_boolean (value);
            break;
        case PROP_BITS:
            {
                guint val;

                val = g_value_get_uint (value);

                if (val != 8 && val != 16 && val != 32) {
                    g_warning ("Write::bits can only 8, 16 or 32");
                    return;
                }

                priv->depth = val;
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_write_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoWriteTaskPrivate *priv = UFO_WRITE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FORMAT:
            g_value_set_string (value, priv->format);
            break;
        case PROP_SINGLE_FILE:
            g_value_set_boolean (value, priv->single);
            break;
        case PROP_APPEND:
            g_value_set_boolean (value, priv->append);
            break;
        case PROP_BITS:
            g_value_set_uint (value, priv->depth);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_write_task_finalize (GObject *object)
{
    UfoWriteTaskPrivate *priv;

    priv = UFO_WRITE_TASK_GET_PRIVATE (object);

    if (priv->template)
        g_free (priv->template);

    if (priv->single)
        TIFFClose (priv->tif);

    g_free (priv->format);
    priv->format= NULL;

    G_OBJECT_CLASS (ufo_write_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_write_task_setup;
    iface->get_num_inputs = ufo_write_task_get_num_inputs;
    iface->get_num_dimensions = ufo_write_task_get_num_dimensions;
    iface->get_mode = ufo_write_task_get_mode;
    iface->get_requisition = ufo_write_task_get_requisition;
    iface->process = ufo_write_task_process;
}

static void
ufo_write_task_class_init (UfoWriteTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_write_task_set_property;
    gobject_class->get_property = ufo_write_task_get_property;
    gobject_class->finalize = ufo_write_task_finalize;

    properties[PROP_FORMAT] =
        g_param_spec_string ("filename",
            "Filename format string",
            "Format string of the path and filename. If multiple files are written it must contain a '%i' specifier denoting the current count",
            "./output-%05i.tif",
            G_PARAM_READWRITE);

    properties[PROP_SINGLE_FILE] =
        g_param_spec_boolean ("single-file",
            "Whether to write a single file or a sequence of files",
            "Whether to write a single file or a sequence of files",
            FALSE,
            G_PARAM_READWRITE);

    properties[PROP_APPEND] =
        g_param_spec_boolean ("append",
            "If true the data is appended, otherwise overwritten",
            "If true the data is appended, otherwise overwritten",
            FALSE,
            G_PARAM_READWRITE);

    properties[PROP_BITS] =
        g_param_spec_uint ("bits",
                           "Number of bits per sample",
                           "Number of bits per sample. Possible values in [8, 16, 32].",
                           8, 32, 32, G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoWriteTaskPrivate));
}

static void
ufo_write_task_init(UfoWriteTask *self)
{
    self->priv = UFO_WRITE_TASK_GET_PRIVATE(self);
    self->priv->format = g_strdup ("./output-%05i.tif");
    self->priv->template = NULL;
    self->priv->counter = 0;
    self->priv->append = FALSE;
    self->priv->single = FALSE;
    self->priv->depth = 32;
}
