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
#include <uca/uca-plugin-manager.h>
#include <uca/uca-camera.h>

#include "ufo-camera-task.h"


struct _UfoCameraTaskPrivate {
    UcaPluginManager *pm;
    UcaCamera  *camera;
    guint       current;
    guint       count;
    guint       width;
    guint       height;
    guint       n_bits;
    double      time;
    gchar      *name;
    GTimer     *timer;
    gboolean    readout;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoCameraTask, ufo_camera_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_CAMERA_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_CAMERA_TASK, UfoCameraTaskPrivate))

enum {
    PROP_X,
    PROP_CAMERA,
    PROP_CAMERA_NAME,
    PROP_COUNT,
    PROP_TIME,
    PROP_READOUT,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_camera_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_CAMERA_TASK, NULL));
}

static UcaCamera *
create_camera (UcaPluginManager *pm,
               const gchar *name,
               GError **error)
{
    UcaCamera *camera;

    if (name == NULL) {
        GList *cameras;
        gchar *first_name;

        cameras = uca_plugin_manager_get_available_cameras (pm);

        if (g_list_length (cameras) == 0) {
            g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                         "No camera found");
            return NULL;
        }

        first_name = (gchar *) g_list_nth_data (cameras, 0);
        camera = uca_plugin_manager_get_camera (pm, first_name, error, NULL);

        g_list_foreach (cameras, (GFunc) g_free, NULL);
        g_list_free (cameras);
    }
    else {
        camera = uca_plugin_manager_get_camera (pm, name, error, NULL);
    }

    return camera;
}

static void
ufo_camera_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoCameraTask *node;
    UfoCameraTaskPrivate *priv;
    gboolean is_recording;

    node = UFO_CAMERA_TASK (task);
    priv = node->priv;

    priv->pm = uca_plugin_manager_new ();

    if (priv->camera == NULL) {
        GError *tmp_error = NULL;
        priv->camera = create_camera (priv->pm, priv->name, &tmp_error);

        if (tmp_error != NULL) {
            g_propagate_error (error, tmp_error);
            return;
        }
    }

    priv->current = 0;
    priv->timer = g_timer_new ();

    g_object_get (priv->camera,
                  "roi-width", &priv->width,
                  "roi-height", &priv->height,
                  "sensor-bitdepth", &priv->n_bits,
                  "is-recording", &is_recording,
                  NULL);

    if (!is_recording && !priv->readout) {
        g_debug ("camera: start recording");
        uca_camera_start_recording (priv->camera, error);
    }

    if (priv->readout) {
        guint n_frames;

        if (is_recording)
            uca_camera_stop_recording (priv->camera, error);

        g_object_get (priv->camera, "recorded-frames", &n_frames, NULL);
        priv->count = n_frames < priv->count ? n_frames : priv->count;
        uca_camera_start_readout (priv->camera, error);
    }
}

static void
ufo_camera_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoCameraTaskPrivate *priv;

    priv = UFO_CAMERA_TASK_GET_PRIVATE (UFO_CAMERA_TASK (task));

    requisition->n_dims = 2;
    requisition->dims[0] = priv->width;
    requisition->dims[1] = priv->height;
}

static guint
ufo_camera_task_get_num_inputs (UfoTask *task)
{
    return 0;
}

static guint
ufo_camera_task_get_num_dimensions (UfoTask *task,
                               guint input)
{
    return 0;
}

static UfoTaskMode
ufo_camera_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_GENERATOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_camera_task_generate (UfoTask *task,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoCameraTaskPrivate *priv;
    GError *tmp_error = NULL;

    priv = UFO_CAMERA_TASK_GET_PRIVATE (UFO_CAMERA_TASK (task));

    if (priv->current < priv->count || g_timer_elapsed (priv->timer, NULL) < priv->time) {
        gfloat *host_array;

        host_array = ufo_buffer_get_host_array (output, NULL);
        uca_camera_grab (priv->camera, (gpointer) host_array, &tmp_error);

        if (tmp_error != NULL) {
            g_warning ("Could not grab frame: %s", tmp_error->message);
            g_error_free (tmp_error);
            return FALSE;
        }

        if (priv->n_bits != 32)
            ufo_buffer_convert (output, priv->n_bits <= 8 ? UFO_BUFFER_DEPTH_8U : UFO_BUFFER_DEPTH_16U);

        priv->current++;
        return TRUE;
    }

    return FALSE;

}

static void
ufo_camera_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoCameraTaskPrivate *priv = UFO_CAMERA_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_CAMERA:
            if (priv->camera)
                g_object_unref (priv->camera);

            priv->camera = UCA_CAMERA (g_value_get_object (value));
            g_object_ref (priv->camera);
            break;
        case PROP_CAMERA_NAME:
            g_free(priv->name);
            priv->name = g_strdup (g_value_get_string (value));
            break;
        case PROP_COUNT:
            priv->count = g_value_get_uint (value);
            break;
        case PROP_TIME:
            priv->time = g_value_get_double (value);
            break;
        case PROP_READOUT:
            priv->readout = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_camera_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoCameraTaskPrivate *priv = UFO_CAMERA_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_CAMERA:
            g_value_set_object (value, priv->camera);
            break;
        case PROP_CAMERA_NAME:
            g_value_set_string (value, priv->name);
            break;
        case PROP_COUNT:
            g_value_set_uint (value, priv->count);
            break;
        case PROP_TIME:
            g_value_set_double (value, priv->time);
            break;
        case PROP_READOUT:
            g_value_set_boolean (value, priv->readout);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_camera_task_dispose (GObject *object)
{
    UfoCameraTaskPrivate *priv = UFO_CAMERA_TASK_GET_PRIVATE (object);

    if (priv->camera != NULL) {
        g_object_unref (priv->camera);
        priv->camera = NULL;
    }

    if (priv->pm != NULL) {
        g_object_unref (priv->pm);
        priv->pm = NULL;
    }

    G_OBJECT_CLASS (ufo_camera_task_parent_class)->dispose (object);
}

static void
ufo_camera_task_finalize (GObject *object)
{
    UfoCameraTaskPrivate *priv = UFO_CAMERA_TASK_GET_PRIVATE (object);

    g_free (priv->name);

    if (priv->timer) {
        g_timer_destroy (priv->timer);
        priv->timer = NULL;
    }

    G_OBJECT_CLASS (ufo_camera_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_camera_task_setup;
    iface->get_requisition = ufo_camera_task_get_requisition;
    iface->get_num_inputs = ufo_camera_task_get_num_inputs;
    iface->get_num_dimensions = ufo_camera_task_get_num_dimensions;
    iface->get_mode = ufo_camera_task_get_mode;
    iface->generate = ufo_camera_task_generate;
}

static void
ufo_camera_task_class_init(UfoCameraTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_camera_task_set_property;
    gobject_class->get_property = ufo_camera_task_get_property;
    gobject_class->dispose = ufo_camera_task_dispose;
    gobject_class->finalize = ufo_camera_task_finalize;

    properties[PROP_CAMERA] =
        g_param_spec_object ("camera",
            "UcaCamera camera object or %NULL",
            "UcaCamera camera object or NULL. %If NULL the camera will be load using the \"name\" property",
            UCA_TYPE_CAMERA,
            G_PARAM_READWRITE);

    properties[PROP_CAMERA_NAME] =
        g_param_spec_string("name",
            "Name of the used camera",
            "Name of the used camera, if none is specified take the first one",
            "",
            G_PARAM_READWRITE);

    properties[PROP_COUNT] =
        g_param_spec_uint("number",
            "Number of frames to record",
            "Number of frames to record",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_TIME] =
        g_param_spec_double("time",
            "Maximum time for recording in fraction of seconds",
            "Maximum time for recording in fraction of seconds",
             0.0, 3600.0, 5.0,
            G_PARAM_READWRITE);

    properties[PROP_READOUT] =
        g_param_spec_boolean("readout",
            "Read-out pre-recorded frames instead of starting the acquisition",
            "Read-out pre-recorded frames instead of starting the acquisition",
            FALSE, G_PARAM_READWRITE);

    for (guint i = PROP_X + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoCameraTaskPrivate));
}

static void
ufo_camera_task_init (UfoCameraTask *self)
{
    UfoCameraTaskPrivate *priv = NULL;

    self->priv = priv = UFO_CAMERA_TASK_GET_PRIVATE (self);
    priv->pm = NULL;
    priv->name = NULL;
    priv->camera = NULL;
    priv->count = 0;
    priv->readout = FALSE;
}
