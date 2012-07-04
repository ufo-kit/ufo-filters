#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-buffer-input.h"

/**
 * SECTION:ufo-filter-buffer-input
 * @Short_description: Output buffers from property
 * @Title: bufferinput
 *
 * Detailed description.
 */

struct _UfoFilterBufferInputPrivate {
    GValueArray *buffers;
    guint        current_buffer;
};

G_DEFINE_TYPE(UfoFilterBufferInput, ufo_filter_buffer_input, UFO_TYPE_FILTER_SOURCE)

#define UFO_FILTER_BUFFER_INPUT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_BUFFER_INPUT, UfoFilterBufferInputPrivate))

enum {
    PROP_0,
    PROP_BUFFERS,
    N_PROPERTIES
};

static GParamSpec *buffer_input_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_buffer_input_initialize (UfoFilterSource *filter, guint **dims, GError **error)
{
    UfoFilterBufferInputPrivate *priv = UFO_FILTER_BUFFER_INPUT_GET_PRIVATE (filter);
    UfoBuffer *input = g_value_get_object (g_value_array_get_nth (priv->buffers, 0));
    guint width, height;

    ufo_buffer_get_2d_dimensions (input, &width, &height);
    dims[0][0] = width;
    dims[0][1] = height;
    priv->current_buffer = 0;
}

static gboolean
ufo_filter_buffer_input_generate_cpu(UfoFilterSource *filter, UfoBuffer *results[], gpointer cmd_queue, GError **error)
{
    UfoFilterBufferInputPrivate *priv = UFO_FILTER_BUFFER_INPUT_GET_PRIVATE(filter);

    if (priv->current_buffer < priv->buffers->n_values) {
        UfoBuffer *input = g_value_get_object(g_value_array_get_nth(priv->buffers, priv->current_buffer++));
        ufo_buffer_swap_host_arrays(input, results[0]);
        return TRUE;
    }

    return FALSE;
}

static void
ufo_filter_buffer_input_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterBufferInputPrivate *priv = UFO_FILTER_BUFFER_INPUT_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_BUFFERS:
            priv->buffers = g_value_array_copy((GValueArray *) g_value_get_boxed(value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_buffer_input_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_buffer_input_finalize(GObject *object)
{
    UfoFilterBufferInputPrivate *priv = UFO_FILTER_BUFFER_INPUT_GET_PRIVATE(object);

    g_value_array_free(priv->buffers);
}

static void
ufo_filter_buffer_input_class_init(UfoFilterBufferInputClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterSourceClass *filter_class = UFO_FILTER_SOURCE_CLASS(klass);

    gobject_class->set_property = ufo_filter_buffer_input_set_property;
    gobject_class->get_property = ufo_filter_buffer_input_get_property;
    gobject_class->finalize = ufo_filter_buffer_input_finalize;
    filter_class->initialize = ufo_filter_buffer_input_initialize;
    filter_class->generate = ufo_filter_buffer_input_generate_cpu;

    /**
     * UfoFilterBufferInput:buffers
     *
     * The buffers property takes a GValueArray of UfoBuffer objects and copies
     * its content. This is only useful for interfacing with the outside world,
     * otherwise you would just connect sources with destinations.
     *
     * However, if you want to use Numpy arrays, you can just pass a list of
     * Numpy arrays converted with ufonp.fromarray() to this filter which is
     * then passing the data further on.
     */
    buffer_input_properties[PROP_BUFFERS] =
        g_param_spec_value_array("buffers",
                "Array of UfoBuffers",
                "Array of UfoBuffers",
                ufo_buffer_param_spec("array",
                    "Numpy array",
                    "Numpy array",
                    NULL,
                    G_PARAM_WRITABLE), G_PARAM_WRITABLE);

    g_object_class_install_property(gobject_class, PROP_BUFFERS, buffer_input_properties[PROP_BUFFERS]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterBufferInputPrivate));
}

static void
ufo_filter_buffer_input_init(UfoFilterBufferInput *self)
{
    UfoFilterBufferInputPrivate *priv = self->priv = UFO_FILTER_BUFFER_INPUT_GET_PRIVATE(self);
    priv->buffers = NULL;

    ufo_filter_register_outputs (UFO_FILTER(self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_BUFFER_INPUT, NULL);
}
