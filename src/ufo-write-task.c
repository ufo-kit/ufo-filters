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
#include <errno.h>

#include "config.h"
#include "ufo-write-task.h"
#include "writers/ufo-writer.h"
#include "writers/ufo-raw-writer.h"

#ifdef HAVE_TIFF
#include "writers/ufo-tiff-writer.h"
#endif

#ifdef WITH_HDF5
#include "writers/ufo-hdf5-writer.h"
#endif

struct _UfoWriteTaskPrivate {
    gchar *filename;
    guint counter;
    gboolean append;
    gsize width;
    gsize height;
    UfoBufferDepth depth;

    gboolean multi_file;
    gboolean opened;

    UfoWriter     *writer;
    UfoRawWriter  *raw_writer;

#ifdef HAVE_TIFF
    UfoTiffWriter *tiff_writer;
#endif

#ifdef WITH_HDF5
    UfoHdf5Writer *hdf5_writer;
    gchar           *dataset;
#endif
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoWriteTask, ufo_write_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_WRITE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_WRITE_TASK, UfoWriteTaskPrivate))

enum {
    PROP_0,
    PROP_FILENAME,
    PROP_APPEND,
    PROP_BITS,
#ifdef WITH_HDF5
    PROP_DATASET,
#endif
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_write_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_WRITE_TASK, NULL));
}

static gchar *
get_current_filename (UfoWriteTaskPrivate *priv)
{
    if (priv->multi_file)
        return g_strdup (priv->filename);

    return g_strdup_printf (priv->filename, priv->counter);
}

static guint
count_format_specifiers (const gchar *filename)
{
    guint count = 0;

    do {
        if (*filename == '%')
            count++;
    } while (*(filename++));

    return count;
}

static void
ufo_write_task_setup (UfoTask *task,
                      UfoResources *resources,
                      GError **error)
{
    UfoWriteTaskPrivate *priv;
    gchar *basename;
    gchar *dirname;
    guint num_fmt_specifiers;

    priv = UFO_WRITE_TASK_GET_PRIVATE (task);
    num_fmt_specifiers = count_format_specifiers (priv->filename);
    basename = g_path_get_basename (priv->filename);
    dirname = g_path_get_dirname (priv->filename);

    if (num_fmt_specifiers > 1) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "`%s' has too many format specifiers", dirname);
        return;
    }

    priv->multi_file = num_fmt_specifiers == 0;

    if (g_str_has_suffix (basename, ".raw")) {
        priv->writer = UFO_WRITER (priv->raw_writer);
    }
#ifdef HAVE_TIFF
    else if (g_str_has_suffix (basename, ".tiff") || g_str_has_suffix (basename, ".tif")) {
        priv->writer = UFO_WRITER (priv->tiff_writer);
    }
#endif
#ifdef WITH_HDF5
    else if (g_str_has_suffix (basename, ".h5")) {
        if (priv->hdf5_writer == NULL) {
            g_error ("write: property ::dataset not specified");
            return;
        }

        priv->writer = UFO_WRITER (priv->hdf5_writer);
    }
#endif
    else {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "`%s' does not have a valid file extension", basename);
        return;
    }

    if (!g_file_test (dirname, G_FILE_TEST_EXISTS)) {
        g_debug ("write: `%s' does not exist. Attempt to create it.", dirname);

        if (g_mkdir_with_parents (dirname, 0755)) {
            g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                         "Could not create `%s'.", dirname);
            return;
        }
    }

    priv->counter = 0;

    if (priv->append && !priv->multi_file) {
        gboolean exists = TRUE;

        while (exists) {
            gchar *filename = get_current_filename (priv);
            exists = g_file_test (filename, G_FILE_TEST_EXISTS);
            g_free (filename);

            if (exists)
                priv->counter++;
        }
    }

    g_free (dirname);
    g_free (basename);
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
    return UFO_TASK_MODE_SINK | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_write_task_process (UfoTask *task,
                        UfoBuffer **inputs,
                        UfoBuffer *output,
                        UfoRequisition *requisition)
{
    UfoWriteTaskPrivate *priv;
    UfoRequisition in_req;
    guint8 *data;
    guint num_frames;
    gsize offset;

    priv = UFO_WRITE_TASK_GET_PRIVATE (UFO_WRITE_TASK (task));
    data = (guint8 *) ufo_buffer_get_host_array (inputs[0], NULL);
    ufo_buffer_get_requisition (inputs[0], &in_req);

    num_frames = in_req.n_dims == 3 ? in_req.dims[2] : 1;
    offset = ufo_buffer_get_size (inputs[0]) / num_frames;

    for (guint i = 0; i < num_frames; i++) {
        if (!priv->multi_file || !priv->opened) {
            gchar *filename = get_current_filename (priv);
            ufo_writer_open (priv->writer, filename);
            g_free (filename);
            priv->opened = TRUE;
        }

        ufo_writer_write (priv->writer, data + i * offset, &in_req, priv->depth);

        if (!priv->multi_file) {
            ufo_writer_close (priv->writer);
            priv->opened = FALSE;
        }

        priv->counter++;
    }

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
        case PROP_FILENAME:
            g_free (priv->filename);
            priv->filename = g_value_dup_string (value);
            break;
        case PROP_APPEND:
            priv->append = g_value_get_boolean (value);
            break;
        case PROP_BITS:
            {
                guint val = g_value_get_uint (value);

                if (val != 8 && val != 16 && val != 32) {
                    g_warning ("Write::bits can only 8, 16 or 32");
                    return;
                }

                if (val == 8)
                    priv->depth = UFO_BUFFER_DEPTH_8U;

                if (val == 16)
                    priv->depth = UFO_BUFFER_DEPTH_16U;

                if (val == 32)
                    priv->depth = UFO_BUFFER_DEPTH_32F;
            }
            break;
#ifdef WITH_HDF5
        case PROP_DATASET:
            g_free (priv->dataset);
            priv->dataset = g_value_dup_string (value);

            if (priv->hdf5_writer != NULL)
                g_object_unref (priv->hdf5_writer);

            priv->hdf5_writer = ufo_hdf5_writer_new (priv->dataset);
            break;
#endif
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
        case PROP_FILENAME:
            g_value_set_string (value, priv->filename);
            break;
        case PROP_APPEND:
            g_value_set_boolean (value, priv->append);
            break;
        case PROP_BITS:
            if (priv->depth == UFO_BUFFER_DEPTH_8U)
                g_value_set_uint (value, 8);

            if (priv->depth == UFO_BUFFER_DEPTH_16U)
                g_value_set_uint (value, 16);

            if (priv->depth == UFO_BUFFER_DEPTH_32F)
                g_value_set_uint (value, 32);
            break;
#ifdef WITH_HDF5
        case PROP_DATASET:
            g_value_set_string (value, priv->dataset);
            break;
#endif
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_write_task_dispose (GObject *object)
{
    UfoWriteTaskPrivate *priv;

    priv = UFO_WRITE_TASK_GET_PRIVATE (object);

    g_object_unref (priv->raw_writer);

#ifdef HAVE_TIFF
    if (priv->tiff_writer)
        g_object_unref (priv->tiff_writer);
#endif

#ifdef WITH_HDF5
    if (priv->hdf5_writer != NULL)
        g_object_unref (priv->hdf5_writer);
#endif

    G_OBJECT_CLASS (ufo_write_task_parent_class)->dispose (object);
}

static void
ufo_write_task_finalize (GObject *object)
{
    UfoWriteTaskPrivate *priv;

    priv = UFO_WRITE_TASK_GET_PRIVATE (object);

    g_free (priv->filename);
    priv->filename= NULL;

#ifdef WITH_HDF5
    g_free (priv->dataset);
#endif

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
    gobject_class->dispose = ufo_write_task_dispose;
    gobject_class->finalize = ufo_write_task_finalize;

    properties[PROP_FILENAME] =
        g_param_spec_string ("filename",
            "Filename filename string",
            "filename string of the path and filename. If multiple files are written it must contain a '%i' specifier denoting the current count",
            "./output-%05i.tif",
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

#ifdef WITH_HDF5
    properties[PROP_DATASET] =
        g_param_spec_string("dataset",
            "Path to an HDF5 dataset",
            "Path to an HDF5 dataset",
            "",
            G_PARAM_READWRITE);
#endif

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoWriteTaskPrivate));
}

static void
ufo_write_task_init(UfoWriteTask *self)
{
    self->priv = UFO_WRITE_TASK_GET_PRIVATE(self);
    self->priv->counter = 0;
    self->priv->append = FALSE;
    self->priv->multi_file = FALSE;
    self->priv->depth = UFO_BUFFER_DEPTH_32F;
    self->priv->writer = NULL;
    self->priv->opened = FALSE;
    self->priv->raw_writer = ufo_raw_writer_new ();

#ifdef HAVE_TIFF
    self->priv->tiff_writer = ufo_tiff_writer_new ();
    self->priv->filename = g_strdup ("./output-%05i.tif");
#else
    self->priv->filename = g_strdup ("./output-%05i.raw");
#endif

#ifdef WITH_HDF5
    self->priv->hdf5_writer = NULL;
    self->priv->dataset = NULL;
#endif
}
