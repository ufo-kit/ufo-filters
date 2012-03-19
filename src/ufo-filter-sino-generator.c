#include <gmodule.h>
#include <string.h>
#include <stdio.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-sino-generator.h"

/**
 * SECTION:ufo-filter-sino-generator
 * @Short_description: Generate sinograms from projections
 * @Title: sinogenerator
 *
 * Reads two-dimensional projections and generates an appropriate amount of
 * sinograms. If all projections are laid on top of each other this results in a
 * rotation of the three-dimensional matrix and slicing again.
 */

struct _UfoFilterSinoGeneratorPrivate {
    guint num_projections;
};

G_DEFINE_TYPE(UfoFilterSinoGenerator, ufo_filter_sino_generator, UFO_TYPE_FILTER)

#define UFO_FILTER_SINO_GENERATOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_SINO_GENERATOR, UfoFilterSinoGeneratorPrivate))

enum {
    PROP_0,
    PROP_NUM_PROJECTIONS,
    N_PROPERTIES
};

static GParamSpec *sino_generator_properties[N_PROPERTIES] = { NULL, };

static void ufo_filter_sino_generator_initialize(UfoFilter *filter)
{
    /* Here you can code, that is called for each newly instantiated filter */
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_sino_generator_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterSinoGeneratorPrivate *priv = UFO_FILTER_SINO_GENERATOR_GET_PRIVATE(filter);
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    /* We pop the very first image, to determine the size w*h of a projection. */
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);
    
    if (input == NULL)
        return;

    guint num_dims = 0;
    guint *dimensions = NULL;
    ufo_buffer_get_dimensions(input, &num_dims, &dimensions);

    const guint num_sinos = dimensions[1]; /* == proj_height */
    const guint sino_width = dimensions[0]; /* == proj_width */
    const guint sino_height = priv->num_projections;

    dimensions[0] = sino_width;
    dimensions[1] = sino_height;
    ufo_channel_allocate_output_buffers(output_channel, 2, dimensions);

    /* XXX: this is critical! */
    float *sinograms = g_malloc0(sizeof(float) * num_sinos * sino_width * sino_height);
    const size_t row_mem_offset = sino_width;
    const size_t sino_mem_offset = row_mem_offset * sino_height;
    guint projection = 1;

    /* First step: collect all projections and build sinograms */
    while ((projection < priv->num_projections) || (input != NULL)) {
        float *src = ufo_buffer_get_host_array(input, command_queue);
        gsize proj_index = 0;
        gsize sino_index = (projection - 1) * row_mem_offset;

        for (guint i = 0; i < num_sinos; i++) {
            memcpy(sinograms + sino_index, src + proj_index, sizeof(float) * row_mem_offset);
            proj_index += row_mem_offset;
            sino_index += sino_mem_offset;
        }

        ufo_channel_finalize_input_buffer(input_channel, input);
        input = ufo_channel_get_input_buffer(input_channel);
        projection++;
    }
    
    /* Second step: push them one by one */
    gsize sino_index = 0;
    for (guint i = 0; i < num_sinos; i++) {
        UfoBuffer *output = ufo_channel_get_output_buffer(output_channel);
        ufo_buffer_set_host_array(output, sinograms + sino_index, sizeof(float) * sino_mem_offset, NULL);
        ufo_channel_finalize_output_buffer(output_channel, output);
        sino_index += sino_mem_offset;
    }

    /* Third step: complete */
    ufo_channel_finish(output_channel);
    g_free(sinograms);
    g_free(dimensions);
}

static void ufo_filter_sino_generator_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterSinoGenerator *self = UFO_FILTER_SINO_GENERATOR(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_NUM_PROJECTIONS:
            self->priv->num_projections = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_sino_generator_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterSinoGenerator *self = UFO_FILTER_SINO_GENERATOR(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_NUM_PROJECTIONS:
            g_value_set_uint(value, self->priv->num_projections);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_sino_generator_class_init(UfoFilterSinoGeneratorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_sino_generator_set_property;
    gobject_class->get_property = ufo_filter_sino_generator_get_property;
    filter_class->initialize = ufo_filter_sino_generator_initialize;
    filter_class->process = ufo_filter_sino_generator_process;

    sino_generator_properties[PROP_NUM_PROJECTIONS] = 
        g_param_spec_uint("num-projections",
            "Number of projections",
            "Number of projections corresponding to the sinogram height",
            0, 8192, 1,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_NUM_PROJECTIONS, sino_generator_properties[PROP_NUM_PROJECTIONS]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterSinoGeneratorPrivate));
}

static void ufo_filter_sino_generator_init(UfoFilterSinoGenerator *self)
{
    UfoFilterSinoGeneratorPrivate *priv = self->priv = UFO_FILTER_SINO_GENERATOR_GET_PRIVATE(self);
    priv->num_projections = 1;

    ufo_filter_register_input(UFO_FILTER(self), "projection", 2);
    ufo_filter_register_output(UFO_FILTER(self), "sinogram", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_SINO_GENERATOR, NULL);
}
