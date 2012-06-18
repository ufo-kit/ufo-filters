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

G_DEFINE_TYPE(UfoFilterAverager, ufo_filter_averager, UFO_TYPE_FILTER)

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

static GError *
ufo_filter_averager_initialize (UfoFilter *filter, UfoBuffer *params[], guint **dims)
{
    UfoFilterAveragerPrivate *priv = UFO_FILTER_AVERAGER_GET_PRIVATE (filter);

    ufo_buffer_get_2d_dimensions (params[0], &priv->width, &priv->height);
    priv->num_input = 0.0f;
    priv->num_pixels = priv->width * priv->height;
    priv->data = g_malloc0 (priv->num_pixels * sizeof (gfloat));

    dims[0][0] = priv->width;
    dims[0][1] = priv->height;

    return NULL;
}

static GError *
ufo_filter_averager_process_cpu (UfoFilter *filter, UfoBuffer *params[], UfoBuffer *results[], gpointer cmd_queue)
{
    UfoFilterAveragerPrivate *priv = UFO_FILTER_AVERAGER_GET_PRIVATE (filter);
    gfloat *in = ufo_buffer_get_host_array (params[0], (cl_command_queue) cmd_queue);

    /* TODO: check that input dims match */
    for (gsize i = 0; i < priv->num_pixels; i++)
        priv->data[i] += in[i];

    priv->num_input += 1.0f;

    return NULL;
}

static GError *
ufo_filter_averager_post_process_cpu (UfoFilter *filter, UfoBuffer *results[], gpointer cmd_queue)
{
    UfoFilterAveragerPrivate *priv = UFO_FILTER_AVERAGER_GET_PRIVATE (filter);

    for (gsize i = 0; i < priv->num_pixels; i++)
        priv->data[i] /= priv->num_input;

    gfloat *out = ufo_buffer_get_host_array (results[0], (cl_command_queue) cmd_queue);
    g_memmove (out, priv->data, priv->num_pixels * sizeof (gfloat));

    return NULL;
}

static void
ufo_filter_averager_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_averager_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
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
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_averager_set_property;
    gobject_class->get_property = ufo_filter_averager_get_property;
    gobject_class->finalize = ufo_filter_averager_finalize;
    filter_class->initialize = ufo_filter_averager_initialize;
    filter_class->process_cpu = ufo_filter_averager_process_cpu;
    filter_class->post_process_cpu = ufo_filter_averager_post_process_cpu;

    g_type_class_add_private(gobject_class, sizeof(UfoFilterAveragerPrivate));
}

static void
ufo_filter_averager_init(UfoFilterAverager *self)
{
    self->priv = UFO_FILTER_AVERAGER_GET_PRIVATE (self);
    self->priv->data = NULL;

    ufo_filter_register_inputs (UFO_FILTER(self), 2, NULL);
    ufo_filter_register_outputs (UFO_FILTER(self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new (UFO_TYPE_FILTER_AVERAGER, NULL);
}
