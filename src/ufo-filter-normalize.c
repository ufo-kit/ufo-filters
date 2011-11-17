#include <gmodule.h>

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-normalize.h"

GType ufo_filter_normalize_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterNormalize, ufo_filter_normalize, UFO_TYPE_FILTER);

#define UFO_FILTER_NORMALIZE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_NORMALIZE, UfoFilterNormalizePrivate))


static void activated(EthosPlugin *plugin)
{
}

static void deactivated(EthosPlugin *plugin)
{
}

/* 
 * virtual methods 
 */
static void ufo_filter_normalize_initialize(UfoFilter *filter)
{
    /* Here you can code, that is called for each newly instantiated filter */
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_normalize_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);
    gint32 dimensions[4] = {1, 1, 1, 1};
    ufo_buffer_get_dimensions(input, dimensions);
    ufo_channel_allocate_output_buffers(output_channel, dimensions);
    const gint32 num_elements = dimensions[0] * dimensions[1] * dimensions[2] * dimensions[3];

    while (input != NULL) {
        float *in_data = ufo_buffer_get_cpu_data(input, command_queue);

        float min = 1.0, max = 0.0;
        for (int i = 0; i < num_elements; i++) {
            if (in_data[i] < min)
                min = in_data[i];
            if (in_data[i] > max)
                max = in_data[i];
        }
        float scale = 1.0 / (max - min);

        UfoBuffer *output = ufo_channel_get_output_buffer(output_channel);
        /* This avoids an unneccessary GPU-to-host transfer */
        ufo_buffer_invalidate_gpu_data(output);
        float *out_data = ufo_buffer_get_cpu_data(output, command_queue);
        for (int i = 0; i < num_elements; i++) {
            out_data[i] = (in_data[i] - min) * scale;
        }
        ufo_channel_finalize_input_buffer(input_channel, input);
        ufo_channel_finalize_output_buffer(output_channel, output);
        input = ufo_channel_get_input_buffer(input_channel);
    }
    ufo_channel_finish(output_channel);
}

static void ufo_filter_normalize_set_property(GObject *object,
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

static void ufo_filter_normalize_get_property(GObject *object,
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

static void ufo_filter_normalize_class_init(UfoFilterNormalizeClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    EthosPluginClass *plugin_class = ETHOS_PLUGIN_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_normalize_set_property;
    gobject_class->get_property = ufo_filter_normalize_get_property;
    plugin_class->activated = activated;
    plugin_class->deactivated = deactivated;
    filter_class->initialize = ufo_filter_normalize_initialize;
    filter_class->process = ufo_filter_normalize_process;
}

static void ufo_filter_normalize_init(UfoFilterNormalize *self)
{
}

G_MODULE_EXPORT EthosPlugin *ethos_plugin_register(void)
{
    return g_object_new(UFO_TYPE_FILTER_NORMALIZE, NULL);
}
