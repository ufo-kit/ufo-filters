#include <gmodule.h>

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-normalize.h"

/**
 * SECTION:ufo-filter-normalize
 * @Short_description: Normalize to [0.0, 1.0]
 * @Title: normalize
 *
 * Normalize input to closed unit interval.
 */

GType ufo_filter_normalize_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterNormalize, ufo_filter_normalize, UFO_TYPE_FILTER);

#define UFO_FILTER_NORMALIZE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_NORMALIZE, UfoFilterNormalizePrivate))


static void ufo_filter_normalize_initialize(UfoFilter *filter)
{
}

static void ufo_filter_normalize_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);
    ufo_channel_allocate_output_buffers_like(output_channel, input);
    const gint32 num_elements = ufo_buffer_get_size(input) / sizeof(float);

    while (input != NULL) {
        float *in_data = ufo_buffer_get_host_array(input, command_queue);

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
        float *out_data = ufo_buffer_get_host_array(output, command_queue);
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
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_normalize_set_property;
    gobject_class->get_property = ufo_filter_normalize_get_property;
    filter_class->initialize = ufo_filter_normalize_initialize;
    filter_class->process = ufo_filter_normalize_process;
}

static void ufo_filter_normalize_init(UfoFilterNormalize *self)
{
    ufo_filter_register_input(UFO_FILTER(self), "image", 2);
    ufo_filter_register_output(UFO_FILTER(self), "image", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_NORMALIZE, NULL);
}
