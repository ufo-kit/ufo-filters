#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <string.h>
#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter-reduce.h>
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

G_DEFINE_TYPE(UfoFilterAverager, ufo_filter_averager, UFO_TYPE_FILTER_REDUCE)

#define UFO_FILTER_AVERAGER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_AVERAGER, UfoFilterAveragerPrivate))

struct _UfoFilterAveragerPrivate {
    guint   width;
    guint   height;
    gfloat  num_input;
    gfloat *data;
    guint   num_pixels;
};

enum {
    PROP_0,
    N_PROPERTIES
};

static void
ufo_filter_averager_initialize (UfoFilterReduce *filter, UfoBuffer *input[], guint **dims, gfloat *default_value, GError **error)
{
    UfoFilterAveragerPrivate *priv = UFO_FILTER_AVERAGER_GET_PRIVATE (filter);

    ufo_buffer_get_2d_dimensions (input[0], &priv->width, &priv->height);
    priv->num_input = 0.0f;
    priv->num_pixels = priv->width * priv->height;
    priv->data = g_malloc0 (priv->num_pixels * sizeof (gfloat));

    dims[0][0] = priv->width;
    dims[0][1] = priv->height;

    *default_value = 3.0f;
}

static void
ufo_filter_averager_collect (UfoFilterReduce *filter, UfoBuffer *input[], UfoBuffer *output[], GError **error)
{
    UfoFilterAveragerPrivate *priv = UFO_FILTER_AVERAGER_GET_PRIVATE (filter);
    cl_command_queue cmd_queue = ufo_filter_get_command_queue (UFO_FILTER (filter));
    gfloat *in = ufo_buffer_get_host_array (input[0], cmd_queue);
    gfloat *out = ufo_buffer_get_host_array (output[0], cmd_queue);

    /* TODO: check that input dims match */
    for (gsize i = 0; i < priv->num_pixels; i++)
        out[i] += in[i];

    priv->num_input += 1.0f;
}

static gboolean
ufo_filter_averager_reduce (UfoFilterReduce *filter, UfoBuffer *output[], GError **error)
{
    UfoFilterAveragerPrivate *priv = UFO_FILTER_AVERAGER_GET_PRIVATE (filter);
    cl_command_queue cmd_queue = ufo_filter_get_command_queue (UFO_FILTER (filter));
    gfloat *out = ufo_buffer_get_host_array (output[0], cmd_queue);

    for (gsize i = 0; i < priv->num_pixels; i++)
        out[i] /= priv->num_input;

    return FALSE;
}

static void
ufo_filter_averager_finalize(GObject *object)
{
    UfoFilterAveragerPrivate *priv = UFO_FILTER_AVERAGER_GET_PRIVATE (object);

    g_free (priv->data);

    G_OBJECT_CLASS(ufo_filter_averager_parent_class)->finalize(object);
}

static void
ufo_filter_averager_class_init(UfoFilterAveragerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterReduceClass *filter_class = UFO_FILTER_REDUCE_CLASS(klass);

    gobject_class->finalize = ufo_filter_averager_finalize;
    filter_class->initialize = ufo_filter_averager_initialize;
    filter_class->collect = ufo_filter_averager_collect;
    filter_class->reduce = ufo_filter_averager_reduce;

    g_type_class_add_private(gobject_class, sizeof(UfoFilterAveragerPrivate));
}

static void
ufo_filter_averager_init(UfoFilterAverager *self)
{
    UfoInputParameter input_params[] = {{2, UFO_FILTER_INFINITE_INPUT}};
    UfoOutputParameter output_params[] = {{2}};

    self->priv = UFO_FILTER_AVERAGER_GET_PRIVATE (self);
    self->priv->data = NULL;

    ufo_filter_register_inputs (UFO_FILTER (self), 1, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new (UFO_TYPE_FILTER_AVERAGER, NULL);
}
