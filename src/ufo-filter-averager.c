#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <string.h>
#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-averager.h"

struct _UfoFilterAveragerPrivate {
};

GType ufo_filter_averager_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterAverager, ufo_filter_averager, UFO_TYPE_FILTER);

#define UFO_FILTER_AVERAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_AVERAGER, UfoFilterAveragerPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

static void activated(EthosPlugin *plugin)
{
}

static void deactivated(EthosPlugin *plugin)
{
}

/* 
 * virtual methods 
 */
static void ufo_filter_averager_initialize(UfoFilter *filter)
{
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_averager_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    gint32 dimensions[4] = {1, 1, 1, 1};
    UfoBuffer *input = ufo_channel_pop(input_channel);

    ufo_buffer_get_dimensions(input, dimensions);
    UfoBuffer *result = ufo_resource_manager_request_buffer(manager, UFO_BUFFER_2D, dimensions, NULL, FALSE);
    float *out = ufo_buffer_get_cpu_data(result, command_queue);
    memset(out, 0, ufo_buffer_get_size(result));

    float num_input = 0.0f;
    const int num_pixels = dimensions[0]*dimensions[1]*dimensions[2]*dimensions[3];

    while (input != NULL) {
        num_input++;
        float *in = ufo_buffer_get_cpu_data(input, command_queue);
        for (int i = 0; i < num_pixels; i++)
            out[i] += in[i];

        input = ufo_channel_pop(input_channel);
    }

    if (num_input > 0.0f) {
        for (int i = 0; i < num_pixels; i++)
            out[i] /= num_input;
    }

    ufo_channel_push(output_channel, result);
    ufo_channel_finish(output_channel);
}

static void ufo_filter_averager_set_property(GObject *object,
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

static void ufo_filter_averager_get_property(GObject *object,
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

static void ufo_filter_averager_class_init(UfoFilterAveragerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    EthosPluginClass *plugin_class = ETHOS_PLUGIN_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_averager_set_property;
    gobject_class->get_property = ufo_filter_averager_get_property;
    plugin_class->activated = activated;
    plugin_class->deactivated = deactivated;
    filter_class->initialize = ufo_filter_averager_initialize;
    filter_class->process = ufo_filter_averager_process;

    /* install private data */
    /* g_type_class_add_private(gobject_class, sizeof(UfoFilterAveragerPrivate)); */
}

static void ufo_filter_averager_init(UfoFilterAverager *self)
{
    /* self->priv = UFO_FILTER_AVERAGER_GET_PRIVATE(self); */
}

G_MODULE_EXPORT EthosPlugin *ethos_plugin_register(void)
{
    return g_object_new(UFO_TYPE_FILTER_AVERAGER, NULL);
}
