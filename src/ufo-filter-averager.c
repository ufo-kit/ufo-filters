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

/**
 * SECTION:ufo-filter-averager
 * @Short_description: Average incoming images
 * @Title: average
 *
 * Sum all incoming images and divide by their number, effectively computing the
 * average of all images.
 */

struct _UfoFilterAveragerPrivate {
};

GType ufo_filter_averager_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterAverager, ufo_filter_averager, UFO_TYPE_FILTER);

#define UFO_FILTER_AVERAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_AVERAGER, UfoFilterAveragerPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

static void ufo_filter_averager_initialize(UfoFilter *filter)
{
}

static void ufo_filter_averager_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);
    ufo_channel_allocate_output_buffers_like(output_channel, input);
    
    UfoBuffer *output = ufo_channel_get_output_buffer(output_channel);
    float *out = ufo_buffer_get_host_array(output, command_queue);
    memset(out, 0, ufo_buffer_get_size(output));

    float num_input = 0.0f;
    const int num_pixels = ufo_buffer_get_size(input) / sizeof(float);

    while (input != NULL) {
        num_input += 1.0f;
        float *in = ufo_buffer_get_host_array(input, command_queue);
        for (int i = 0; i < num_pixels; i++)
            out[i] += in[i];

        ufo_channel_finalize_input_buffer(input_channel, input);
        input = ufo_channel_get_input_buffer(input_channel);
    }

    if (num_input > 0.0f) {
        for (int i = 0; i < num_pixels; i++)
            out[i] /= num_input;
    }

    ufo_channel_finalize_output_buffer(output_channel, output);
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
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_averager_set_property;
    gobject_class->get_property = ufo_filter_averager_get_property;
    filter_class->initialize = ufo_filter_averager_initialize;
    filter_class->process = ufo_filter_averager_process;
}

static void ufo_filter_averager_init(UfoFilterAverager *self)
{
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_AVERAGER, NULL);
}
