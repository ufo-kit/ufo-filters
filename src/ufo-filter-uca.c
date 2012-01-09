#include <gmodule.h>
#include <uca/uca.h>

#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include <ufo/ufo-resource-manager.h>

#include "ufo-filter-uca.h"

struct _UfoFilterUCAPrivate {
    struct uca *u;
    struct uca_camera *cam;
    gint count;
    double time;
};

GType ufo_filter_uca_get_type(void) G_GNUC_CONST;

/* Inherit from UFO_TYPE_FILTER */
G_DEFINE_TYPE(UfoFilterUCA, ufo_filter_uca, UFO_TYPE_FILTER);

#define UFO_FILTER_UCA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_UCA, UfoFilterUCAPrivate))

enum {
    PROP_0,
    PROP_COUNT,
    PROP_TIME,
    N_PROPERTIES
};

static GParamSpec *uca_properties[N_PROPERTIES] = { NULL, };


static void ufo_filter_uca_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterUCA *filter = UFO_FILTER_UCA(object);

    switch (property_id) {
        case PROP_COUNT:
            filter->priv->count = g_value_get_int(value);
            break;
        case PROP_TIME:
            filter->priv->time = g_value_get_double(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_uca_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterUCA *filter = UFO_FILTER_UCA(object);

    switch (property_id) {
        case PROP_COUNT:
            g_value_set_int(value, filter->priv->count);
            break;
        case PROP_TIME:
            g_value_set_double(value, filter->priv->time);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_uca_dispose(GObject *object)
{
    UfoFilterUCAPrivate *priv = UFO_FILTER_UCA_GET_PRIVATE(object);
    g_message("stop recording and camera");
    uca_cam_stop_recording(priv->cam);
    uca_destroy(priv->u);

    G_OBJECT_CLASS(ufo_filter_uca_parent_class)->dispose(object);
}

static void ufo_filter_uca_process(UfoFilter *self)
{
    g_return_if_fail(UFO_IS_FILTER(self));
    UfoFilterUCAPrivate *priv = UFO_FILTER_UCA_GET_PRIVATE(self);
    UfoChannel *output_channel = ufo_filter_get_output_channel(self);

    /* Camera subsystem could not be initialized, so flag end */
    if (priv->u == NULL) {
        g_debug("Camera system is not initialized");
        ufo_channel_finish(output_channel);
        return;
    }

    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(self);
    struct uca_camera *cam = priv->cam;

    uint32_t width, height, bits;
    uca_cam_get_property(cam, UCA_PROP_WIDTH, &width, 0);
    uca_cam_get_property(cam, UCA_PROP_HEIGHT, &height, 0);
    uca_cam_get_property(cam, UCA_PROP_BITDEPTH, &bits, 0);
    gint32 dimensions[4] = { width, height, 1, 1 };
    ufo_channel_allocate_output_buffers(output_channel, dimensions);

    uca_cam_start_recording(cam);
    GTimer *timer = g_timer_new();

    for (guint i = 0; i < priv->count || g_timer_elapsed(timer, NULL) < priv->time; i++) {
        UfoBuffer *output = ufo_channel_get_output_buffer(output_channel);
        uca_cam_grab(cam, (char *) ufo_buffer_get_cpu_data(output, command_queue), NULL);

        ufo_buffer_reinterpret(output, bits, width * height);
        ufo_channel_finalize_output_buffer(output_channel, output);
    }

    g_timer_destroy(timer);
    ufo_channel_finish(output_channel);
}

static void ufo_filter_uca_class_init(UfoFilterUCAClass *klass)
{
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = ufo_filter_uca_set_property;
    gobject_class->get_property = ufo_filter_uca_get_property;
    gobject_class->dispose = ufo_filter_uca_dispose;
    filter_class->process = ufo_filter_uca_process;

    uca_properties[PROP_COUNT] =
        g_param_spec_int("count",
        "Number of frames to record",
        "Number of frames to record",
        0,     /* minimum */
        8192,   /* maximum */
        0,     /* default */
        G_PARAM_READWRITE);

    uca_properties[PROP_TIME] = g_param_spec_double("time",
        "Maximum time for recording in fraction of seconds",
        "Maximum time for recording in fraction of seconds",
         0.0,       /* minimum */
         3600.0,    /* maximum */
         5.0,       /* default */
        G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_COUNT, uca_properties[PROP_COUNT]);
    g_object_class_install_property(gobject_class, PROP_TIME, uca_properties[PROP_TIME]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterUCAPrivate));
}

static void ufo_filter_uca_init(UfoFilterUCA *self)
{
    self->priv = UFO_FILTER_UCA_GET_PRIVATE(self);
    self->priv->cam = NULL;
    /* FIXME: what to do when u == NULL? */
    self->priv->u = uca_init(NULL);
    if (self->priv->u == NULL)
        return;

    self->priv->cam = self->priv->u->cameras;
    self->priv->count = 0;
    uca_cam_alloc(self->priv->cam, 10);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_UCA, NULL);
}
