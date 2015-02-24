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
#include <glob.h>

#include "config.h"
#include "ufo-read-task.h"

#include "readers/ufo-reader.h"
#include "readers/ufo-edf-reader.h"

#ifdef HAVE_TIFF
#include "readers/ufo-tiff-reader.h"
#endif


struct _UfoReadTaskPrivate {
    gchar   *path;
    GList   *filenames;
    GList   *current_element;
    guint    current;
    guint    step;
    guint    start;
    guint    end;
    gboolean done;

    UfoBufferDepth  depth;
    gboolean convert;

    guint    roi_y;
    guint    roi_height;
    guint    roi_step;

    UfoReader       *reader;
    UfoEdfReader    *edf_reader;

#ifdef HAVE_TIFF
    UfoTiffReader   *tiff_reader;
#endif
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoReadTask, ufo_read_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_READ_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_READ_TASK, UfoReadTaskPrivate))

enum {
    PROP_0,
    PROP_PATH,
    PROP_START,
    PROP_END,
    PROP_STEP,
    PROP_ROI_Y,
    PROP_ROI_HEIGHT,
    PROP_ROI_STEP,
    PROP_CONVERT,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_read_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_READ_TASK, NULL));
}

static GList *
read_filenames (const gchar *path)
{
    GList *result;
    gchar *pattern;
    glob_t filenames;

    result = NULL;

    if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
        /* This is a single file without any asterisks */
        pattern = g_strdup (path);
    }
    else {
        /* This is a directory which we may have to glob */
        pattern = strstr (path, "*") != NULL ? g_strdup (path) : g_build_filename (path, "*", NULL);
    }

    glob (pattern, GLOB_MARK | GLOB_TILDE, NULL, &filenames);

    for (guint i = 0; i < filenames.gl_pathc; i++) {
        const gchar *filename = filenames.gl_pathv[i];

#ifdef HAVE_TIFF
        if (g_str_has_suffix (filename, ".tiff") || g_str_has_suffix (filename, ".tif"))
            result = g_list_append (result, g_strdup (filename));
#endif

        if (g_str_has_suffix (filename, ".edf"))
            result = g_list_append (result, g_strdup (filename));
    }

    globfree (&filenames);
    g_free (pattern);
    return result;
}

static void
ufo_read_task_setup (UfoTask *task,
                     UfoResources *resources,
                     GError **error)
{
    UfoReadTaskPrivate *priv;

    priv = UFO_READ_TASK_GET_PRIVATE (task);

    if (priv->end <= priv->start) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "End must be less than start");
        return;
    }

    priv->filenames = read_filenames (priv->path);

    if (priv->filenames == NULL) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "`%s' does not match any files", priv->path);
        return;
    }

    priv->filenames = g_list_sort (priv->filenames, (GCompareFunc) g_strcmp0);
    priv->current_element = g_list_nth (priv->filenames, priv->start);
}

static UfoReader *
get_reader (UfoReadTaskPrivate *priv, const gchar *filename)
{
#ifdef HAVE_TIFF
    if (g_str_has_suffix (filename, ".tiff") || g_str_has_suffix (filename, ".tif")) {
        return UFO_READER (priv->tiff_reader);
    }
#endif

    if (g_str_has_suffix (filename, ".edf")) {
        return UFO_READER (priv->edf_reader);
    }

    return NULL;
}

static void
ufo_read_task_get_requisition (UfoTask *task,
                               UfoBuffer **inputs,
                               UfoRequisition *requisition)
{
    UfoReadTaskPrivate *priv;
    gsize width;
    gsize height;
    const gchar *filename;

    priv = UFO_READ_TASK_GET_PRIVATE (UFO_READ_TASK (task));

    if (priv->reader == NULL) {
        filename = (gchar *) priv->current_element->data;
        priv->reader = get_reader (priv, filename);
        ufo_reader_open (priv->reader, filename);
    }

    if (!ufo_reader_data_available (priv->reader)) {
        ufo_reader_close (priv->reader);
        priv->current_element = g_list_nth (priv->current_element, priv->step);

        if (priv->current_element == NULL) {
            priv->done = TRUE;
            priv->reader = NULL;
            return;
        }
        else {
            filename = (gchar *) priv->current_element->data;
            priv->reader = get_reader (priv, filename);
            ufo_reader_open (priv->reader, filename);
        }
    }

    ufo_reader_get_meta (priv->reader, &width, &height, &priv->depth);

    if (priv->roi_y >= height) {
        g_warning ("read: vertical ROI start %i >= height %zu", priv->roi_y, height);
        priv->roi_y = 0;
    }

    if (!priv->roi_height) {
        priv->roi_height = height - priv->roi_y;
    }
    else {
        if (priv->roi_y + priv->roi_height > height) {
            g_warning ("read: vertical ROI height %i >= height %zu", priv->roi_height, height);
            priv->roi_height = height - priv->roi_y;
        }
    }

    requisition->n_dims = 2;
    requisition->dims[0] = width;
    requisition->dims[1] = priv->roi_height / priv->roi_step;
}

static guint
ufo_read_task_get_num_inputs (UfoTask *task)
{
    return 0;
}

static guint
ufo_read_task_get_num_dimensions (UfoTask *task,
                               guint input)
{
    return 0;
}

static UfoTaskMode
ufo_read_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_GENERATOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_read_task_generate (UfoTask *task,
                        UfoBuffer *output,
                        UfoRequisition *requisition)
{
    UfoReadTaskPrivate *priv;

    priv = UFO_READ_TASK_GET_PRIVATE (UFO_READ_TASK (task));

    if (priv->current == priv->end || priv->done)
        return FALSE;

    ufo_reader_read (priv->reader, output, requisition, priv->roi_y, priv->roi_height, priv->roi_step);

    if ((priv->depth != UFO_BUFFER_DEPTH_32F) && priv->convert)
        ufo_buffer_convert (output, priv->depth);

    priv->current++;
    return TRUE;
}

static void
ufo_read_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoReadTaskPrivate *priv = UFO_READ_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_PATH:
            g_free (priv->path);
            priv->path = g_value_dup_string (value);
            break;
        case PROP_STEP:
            priv->step = g_value_get_uint (value);
            break;
        case PROP_ROI_Y:
            priv->roi_y = g_value_get_uint (value);
            break;
        case PROP_ROI_HEIGHT:
            priv->roi_height = g_value_get_uint (value);
            break;
        case PROP_ROI_STEP:
            priv->roi_step = g_value_get_uint (value);
            break;
        case PROP_CONVERT:
            priv->convert = g_value_get_boolean (value);
            break;
        case PROP_START:
            priv->start = g_value_get_uint (value);
            break;
        case PROP_END:
            priv->end = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_read_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoReadTaskPrivate *priv = UFO_READ_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_PATH:
            g_value_set_string (value, priv->path);
            break;
        case PROP_STEP:
            g_value_set_uint (value, priv->step);
            break;
        case PROP_ROI_Y:
            g_value_set_uint (value, priv->roi_y);
            break;
        case PROP_ROI_HEIGHT:
            g_value_set_uint (value, priv->roi_height);
            break;
        case PROP_ROI_STEP:
            g_value_set_uint (value, priv->roi_step);
            break;
        case PROP_CONVERT:
            g_value_set_boolean (value, priv->convert);
            break;
        case PROP_START:
            g_value_set_uint (value, priv->start);
            break;
        case PROP_END:
            g_value_set_uint (value, priv->end);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_read_task_dispose (GObject *object)
{
    UfoReadTaskPrivate *priv;

    priv = UFO_READ_TASK_GET_PRIVATE (object);

    g_object_unref (priv->edf_reader);

#ifdef HAVE_TIFF
    g_object_unref (priv->tiff_reader);
#endif
}

static void
ufo_read_task_finalize (GObject *object)
{
    UfoReadTaskPrivate *priv;

    priv = UFO_READ_TASK_GET_PRIVATE (object);

    g_free (priv->path);
    priv->path = NULL;

    if (priv->filenames != NULL) {
        g_list_free_full (priv->filenames, (GDestroyNotify) g_free);
        priv->filenames = NULL;
    }

    G_OBJECT_CLASS (ufo_read_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_read_task_setup;
    iface->get_num_inputs = ufo_read_task_get_num_inputs;
    iface->get_num_dimensions = ufo_read_task_get_num_dimensions;
    iface->get_mode = ufo_read_task_get_mode;
    iface->get_requisition = ufo_read_task_get_requisition;
    iface->generate = ufo_read_task_generate;
}

static void
ufo_read_task_class_init(UfoReadTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_read_task_set_property;
    gobject_class->get_property = ufo_read_task_get_property;
    gobject_class->dispose = ufo_read_task_dispose;
    gobject_class->finalize = ufo_read_task_finalize;

    properties[PROP_PATH] =
        g_param_spec_string("path",
            "Glob-style pattern.",
            "Glob-style pattern that describes the file path.",
            "*.tif",
            G_PARAM_READWRITE);

    properties[PROP_STEP] =
        g_param_spec_uint("step",
        "Read every \"step\" file",
        "Read every \"step\" file",
        1, G_MAXUINT, 1,
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

    properties[PROP_ROI_STEP] =
        g_param_spec_uint("y-step",
            "Read every \"step\" row",
            "Read every \"step\" row",
            1, G_MAXUINT, 1,
            G_PARAM_READWRITE);

    properties[PROP_CONVERT] =
        g_param_spec_boolean("enable-conversion",
            "Enable automatic conversion",
            "Enable automatic conversion of input data types to float",
            TRUE,
            G_PARAM_READWRITE);

    properties[PROP_START] =
        g_param_spec_uint("start",
            "Offset to the first read file",
            "Offset to the first read file",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_END] =
        g_param_spec_uint("end",
            "The files will be read until \"end\" - 1 index",
            "The files will be read until \"end\" - 1 index",
            1, G_MAXUINT, G_MAXUINT,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoReadTaskPrivate));
}

static void
ufo_read_task_init(UfoReadTask *self)
{
    UfoReadTaskPrivate *priv = NULL;

    self->priv = priv = UFO_READ_TASK_GET_PRIVATE (self);
    priv->path = g_strdup (".");
    priv->step = 1;
    priv->roi_y = 0;
    priv->roi_height = 0;
    priv->roi_step = 1;
    priv->convert = TRUE;
    priv->start = 0;
    priv->end = G_MAXUINT;
    priv->depth = UFO_BUFFER_DEPTH_32F;

    priv->edf_reader = ufo_edf_reader_new ();

#ifdef HAVE_TIFF
    priv->tiff_reader = ufo_tiff_reader_new ();
#endif

    priv->reader = NULL;
    priv->done = FALSE;
}
