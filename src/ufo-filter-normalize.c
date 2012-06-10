#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

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

G_DEFINE_TYPE(UfoFilterNormalize, ufo_filter_normalize, UFO_TYPE_FILTER)

#define UFO_FILTER_NORMALIZE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_NORMALIZE, UfoFilterNormalizePrivate))

static GError *ufo_filter_normalize_initialize(UfoFilter *filter, UfoBuffer *inputs[], guint **dims)
{
    ufo_buffer_get_2d_dimensions (inputs[0], &dims[0][0], &dims[0][1]);
    return NULL;
}

static GError *ufo_filter_normalize_process_cpu(UfoFilter *filter,
        UfoBuffer *inputs[], UfoBuffer *outputs[], gpointer cmd_queue)
{
    const gsize num_elements = ufo_buffer_get_size(inputs[0]) / sizeof(float);
    float *in_data = ufo_buffer_get_host_array(inputs[0], (cl_command_queue) cmd_queue);
    float min = 1.0, max = 0.0;

    for (guint i = 0; i < num_elements; i++) {
        if (in_data[i] < min)
            min = in_data[i];
        if (in_data[i] > max)
            max = in_data[i];
    }

    float scale = 1.0f / (max - min);
    float *out_data = ufo_buffer_get_host_array(outputs[0], (cl_command_queue) cmd_queue);

    for (guint i = 0; i < num_elements; i++) 
        out_data[i] = (in_data[i] - min) * scale;

    return NULL;
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
    filter_class->process_cpu = ufo_filter_normalize_process_cpu;
}

static void ufo_filter_normalize_init(UfoFilterNormalize *self)
{
    ufo_filter_register_inputs(UFO_FILTER(self), 2, NULL);
    ufo_filter_register_outputs(UFO_FILTER(self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_NORMALIZE, NULL);
}
