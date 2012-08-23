#include <gmodule.h>
#include <uca/uca-camera.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include <ufo/ufo-resource-manager.h>

#include "ufo-filter-cam-access.h"

/**
 * SECTION:ufo-filter-cam-access
 * @Short_description: Provide images from 2D detectors
 * @Title: camaccess
 *
 * This node reads images from cameras supported by libuca. This filter is only
 * built if libuca was found at build time.
 */

struct _UfoFilterCamAccessPrivate {
    UcaCamera  *camera;
    guint       current;
    guint       count;
    double      time;
    gchar      *name;
    GTimer     *timer;
};

G_DEFINE_TYPE(UfoFilterCamAccess, ufo_filter_cam_access, UFO_TYPE_FILTER_SOURCE)

#define UFO_FILTER_CAM_ACCESS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_CAM_ACCESS, UfoFilterCamAccessPrivate))

enum {
    PROP_X,
    PROP_NAME,
    PROP_COUNT,
    PROP_TIME,
    N_PROPERTIES
};

static GParamSpec *uca_properties[N_PROPERTIES] = { NULL, };


static void
ufo_filter_cam_access_initialize(UfoFilterSource *filter, guint **dims, GError **error)
{
    UfoFilterCamAccessPrivate *priv = UFO_FILTER_CAM_ACCESS_GET_PRIVATE (filter);
    GError *tmp_error = NULL;
    gchar *name = NULL;

    if (priv->name == NULL) {
        gchar **types = uca_camera_get_types();

        if (types[0] == NULL) {
            /* TODO: issue a real error */
            g_warning("No camera available");
            return;
        }

        name = g_strdup(types[0]);
        g_strfreev(types);
    }
    else {
        name = g_strdup(priv->name);
    }

    priv->current = 0;
    priv->timer = g_timer_new ();
    priv->camera = uca_camera_new (name, &tmp_error);
    g_free(name);

    if (tmp_error != NULL) {
        g_propagate_error (error, tmp_error);
        return;
    }

    g_object_get (priv->camera, "roi-width", &dims[0][0], "roi-height", &dims[0][1], NULL);

    uca_camera_start_recording(priv->camera, &tmp_error);

    if (tmp_error != NULL)
        g_propagate_error (error, tmp_error);
}

static gboolean
ufo_filter_cam_access_generate(UfoFilterSource *filter, UfoBuffer *results[], GError **error)
{
    UfoFilterCamAccessPrivate *priv = UFO_FILTER_CAM_ACCESS_GET_PRIVATE(filter);
    GError *tmp_error = NULL;

    if (priv->current < priv->count && g_timer_elapsed (priv->timer, NULL) < priv->time) {
        cl_command_queue cmd_queue = ufo_filter_get_command_queue (UFO_FILTER (filter));
        gfloat *host_buffer = ufo_buffer_get_host_array (results[0], cmd_queue);
        uca_camera_grab (priv->camera, (gpointer) &host_buffer, &tmp_error);

        if (tmp_error != NULL)
            g_propagate_error (error, tmp_error);

        priv->current++;
        return TRUE;
    }

    return FALSE;
}

static void
ufo_filter_cam_access_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterCamAccessPrivate *priv = UFO_FILTER_CAM_ACCESS_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_NAME:
            g_free(priv->name);
            priv->name = g_strdup (g_value_get_string (value));
            break;
        case PROP_COUNT:
            priv->count = g_value_get_uint (value);
            break;
        case PROP_TIME:
            priv->time = g_value_get_double (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_cam_access_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterCamAccessPrivate *priv = UFO_FILTER_CAM_ACCESS_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_NAME:
            g_value_set_string (value, priv->name);
            break;
        case PROP_COUNT:
            g_value_set_uint (value, priv->count);
            break;
        case PROP_TIME:
            g_value_set_double (value, priv->time);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_cam_access_dispose (GObject *object)
{
    UfoFilterCamAccessPrivate *priv = UFO_FILTER_CAM_ACCESS_GET_PRIVATE(object);

    uca_camera_stop_recording(priv->camera, NULL);
    g_object_unref(priv->camera);

    G_OBJECT_CLASS(ufo_filter_cam_access_parent_class)->dispose(object);
}

static void
ufo_filter_cam_access_finalize (GObject *object)
{
    UfoFilterCamAccessPrivate *priv = UFO_FILTER_CAM_ACCESS_GET_PRIVATE(object);

    g_timer_destroy (priv->timer);
    g_free (priv->name);

    G_OBJECT_CLASS(ufo_filter_cam_access_parent_class)->finalize(object);
}

static void
ufo_filter_cam_access_class_init(UfoFilterCamAccessClass *klass)
{
    UfoFilterSourceClass *filter_class = UFO_FILTER_SOURCE_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = ufo_filter_cam_access_set_property;
    gobject_class->get_property = ufo_filter_cam_access_get_property;
    gobject_class->dispose = ufo_filter_cam_access_dispose;
    gobject_class->finalize = ufo_filter_cam_access_finalize;
    filter_class->initialize = ufo_filter_cam_access_initialize;
    filter_class->generate = ufo_filter_cam_access_generate;

    uca_properties[PROP_NAME] =
        g_param_spec_string("name",
            "Name of the used camera",
            "Name of the used camera, if none is specified take the first one",
            "", 
            G_PARAM_READWRITE);

    uca_properties[PROP_COUNT] =
        g_param_spec_uint("count",
            "Number of frames to record",
            "Number of frames to record",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    uca_properties[PROP_TIME] = 
        g_param_spec_double("time",
            "Maximum time for recording in fraction of seconds",
            "Maximum time for recording in fraction of seconds",
             0.0, 3600.0, 5.0,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_NAME, uca_properties[PROP_NAME]);
    g_object_class_install_property(gobject_class, PROP_COUNT, uca_properties[PROP_COUNT]);
    g_object_class_install_property(gobject_class, PROP_TIME, uca_properties[PROP_TIME]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterCamAccessPrivate));
}

static void
ufo_filter_cam_access_init(UfoFilterCamAccess *self)
{
    UfoOutputParameter output_params[] = {{2}};

    self->priv = UFO_FILTER_CAM_ACCESS_GET_PRIVATE(self);
    self->priv->name = NULL;
    self->priv->camera = NULL;
    self->priv->count = 0;

    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_CAM_ACCESS, NULL);
}
