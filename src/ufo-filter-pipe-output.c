#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-pipe-output.h"

struct _UfoFilterPipeOutputPrivate {
    gchar *pipe_name;
};

GType ufo_filter_pipe_output_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterPipeOutput, ufo_filter_pipe_output, UFO_TYPE_FILTER);

#define UFO_FILTER_PIPE_OUTPUT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_PIPE_OUTPUT, UfoFilterPipeOutputPrivate))

enum {
    PROP_0,
    PROP_PIPE_NAME,
    N_PROPERTIES
};

static GParamSpec *pipe_output_properties[N_PROPERTIES] = { NULL, };


static void ufo_filter_pipe_output_initialize(UfoFilter *filter)
{
}

static void ufo_filter_pipe_output_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterPipeOutputPrivate *priv = UFO_FILTER_PIPE_OUTPUT_GET_PRIVATE(filter);

    if (priv->pipe_name == NULL)
        return;

    int fd = open(priv->pipe_name, O_WRONLY);

    cl_command_queue command_queue = ufo_filter_get_command_queue(filter);

    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);

    guint *dim_size = NULL;
    guint num_dims = 0;

    while (input != NULL) {
        ufo_buffer_get_dimensions(input, &num_dims, &dim_size);
        const gsize size = sizeof(float) * dim_size[0] * dim_size[1];
        void *data = (void *) ufo_buffer_get_host_array(input, command_queue);
        gsize written = 0;

        while (written < size) {
            gsize result = write(fd, data + written, size - written);

            if (result < 0) {
                g_error("Error writing to pipe %s\n", priv->pipe_name);
                ufo_channel_finalize_input_buffer(input_channel, input);
                return;
            }

            written += result;
        }

        ufo_channel_finalize_input_buffer(input_channel, input);
        input = ufo_channel_get_input_buffer(input_channel);
    }

    close(fd);
}

static void ufo_filter_pipe_output_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterPipeOutputPrivate *priv = UFO_FILTER_PIPE_OUTPUT_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_PIPE_NAME:
            g_free(priv->pipe_name);
            priv->pipe_name = g_value_dup_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_pipe_output_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterPipeOutputPrivate *priv = UFO_FILTER_PIPE_OUTPUT_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_PIPE_NAME:
            g_value_set_string(value, priv->pipe_name);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_pipe_output_class_init(UfoFilterPipeOutputClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_pipe_output_set_property;
    gobject_class->get_property = ufo_filter_pipe_output_get_property;
    filter_class->initialize = ufo_filter_pipe_output_initialize;
    filter_class->process = ufo_filter_pipe_output_process;

    pipe_output_properties[PROP_PIPE_NAME] = 
        g_param_spec_string("pipe-name",
            "Path to the named pipe created with mkfifo",
            "Path to the named pipe created with mkfifo",
            "",
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_PIPE_NAME, pipe_output_properties[PROP_PIPE_NAME]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterPipeOutputPrivate));
}

static void ufo_filter_pipe_output_init(UfoFilterPipeOutput *self)
{
    UfoFilterPipeOutputPrivate *priv = self->priv = UFO_FILTER_PIPE_OUTPUT_GET_PRIVATE(self);
    priv->pipe_name = NULL;

    ufo_filter_register_input(UFO_FILTER(self), "input", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_PIPE_OUTPUT, NULL);
}

