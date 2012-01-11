#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-arg-max.h"

GType ufo_filter_arg_max_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterArgMax, ufo_filter_arg_max, UFO_TYPE_FILTER);

#define UFO_FILTER_ARG_MAX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_ARG_MAX, UfoFilterArgMaxPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

static void ufo_filter_arg_max_initialize(UfoFilter *filter)
{
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_arg_max_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);

    while (input != NULL) {
        /* FIXME: redo this in a general way */
        ufo_channel_finalize_input_buffer(input_channel, input);
        input = ufo_channel_get_input_buffer(input_channel);
    }
}

static void ufo_filter_arg_max_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_arg_max_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_arg_max_class_init(UfoFilterArgMaxClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_arg_max_set_property;
    gobject_class->get_property = ufo_filter_arg_max_get_property;
    filter_class->initialize = ufo_filter_arg_max_initialize;
    filter_class->process = ufo_filter_arg_max_process;
}

static void ufo_filter_arg_max_init(UfoFilterArgMax *self)
{
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_ARG_MAX, NULL);
}
