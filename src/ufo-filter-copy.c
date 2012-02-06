#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-copy.h"

struct _UfoFilterCopyPrivate {
    guint num_outputs;
};

GType ufo_filter_copy_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterCopy, ufo_filter_copy, UFO_TYPE_FILTER);

#define UFO_FILTER_COPY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_COPY, UfoFilterCopyPrivate))

enum {
    PROP_0,
    PROP_OUTPUTS,
    N_PROPERTIES
};

static GParamSpec *copy_properties[N_PROPERTIES] = { NULL, };

static void ufo_filter_copy_initialize(UfoFilter *filter)
{
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_copy_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterCopyPrivate *priv = UFO_FILTER_COPY_GET_PRIVATE(filter);
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);

    if (input == NULL)
        return;

    UfoChannel **output_channels = g_malloc0(priv->num_outputs * sizeof(UfoChannel *));
    char channel_name[256];
    cl_command_queue command_queue = ufo_filter_get_command_queue(filter);

    for (guint i = 0; i < priv->num_outputs; i++) {
        g_snprintf(channel_name, 256, "output%i", i); 
        output_channels[i] = ufo_filter_get_output_channel_by_name(filter, channel_name);
        ufo_channel_allocate_output_buffers_like(output_channels[i], input);
    }

    while (input != NULL) {
        for (int i = 0; i < priv->num_outputs; i++) {
            UfoBuffer *output = ufo_channel_get_output_buffer(output_channels[i]);
            ufo_buffer_copy(input, output, command_queue);
            ufo_channel_finalize_output_buffer(output_channels[i], output);
        }

        ufo_channel_finalize_input_buffer(input_channel, input);
        input = ufo_channel_get_input_buffer(input_channel);
    }

    for (guint i = 0; i < priv->num_outputs; i++)
        ufo_channel_finish(output_channels[i]);

    g_free(output_channels);
}

static void ufo_filter_copy_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterCopyPrivate *priv = UFO_FILTER_COPY_GET_PRIVATE(object);
    char output_name[256];

    switch (property_id) {
        case PROP_OUTPUTS:
            priv->num_outputs = g_value_get_int(value);
            /* FIXME: Output must depend on input type! */
            for (guint i = 0; i < priv->num_outputs; i++) {
                g_snprintf(output_name, 256, "output%i", i); 
                ufo_filter_register_output(UFO_FILTER(object), output_name, 2);
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_copy_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterCopy *self = UFO_FILTER_COPY(object);

    switch (property_id) {
        case PROP_OUTPUTS:
            g_value_set_int(value, self->priv->num_outputs);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_copy_class_init(UfoFilterCopyClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_copy_set_property;
    gobject_class->get_property = ufo_filter_copy_get_property;
    filter_class->initialize = ufo_filter_copy_initialize;
    filter_class->process = ufo_filter_copy_process;

    copy_properties[PROP_OUTPUTS] = 
        g_param_spec_int("outputs",
            "Number of outputs",
            "This filter copies the input to output channels \"output 1\" to \"output [outputs]\"",
            1, 1024, 2,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_OUTPUTS, copy_properties[PROP_OUTPUTS]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterCopyPrivate));
}

static void ufo_filter_copy_init(UfoFilterCopy *self)
{
    UfoFilterCopyPrivate *priv = self->priv = UFO_FILTER_COPY_GET_PRIVATE(self);
    priv->num_outputs = 2;

    ufo_filter_register_input(UFO_FILTER(self), "image", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output0", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output1", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_COPY, NULL);
}
