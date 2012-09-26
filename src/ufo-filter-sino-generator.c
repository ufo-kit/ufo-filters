#include <gmodule.h>
#include <string.h>
#include <stdio.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter-reduce.h>
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
    guint   num_projections;
    guint   num_sinos;
    guint   current_sino;
    guint   sino_width;
    gfloat *sinograms;
    gsize   projection;
    gsize   sino_mem_offset;
};

G_DEFINE_TYPE(UfoFilterSinoGenerator, ufo_filter_sino_generator, UFO_TYPE_FILTER_REDUCE)

#define UFO_FILTER_SINO_GENERATOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), UFO_TYPE_FILTER_SINO_GENERATOR, UfoFilterSinoGeneratorPrivate))

enum {
    PROP_0,
    PROP_NUM_PROJECTIONS,
    N_PROPERTIES
};

static GParamSpec *sino_generator_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_sino_generator_initialize (UfoFilterReduce *filter, UfoBuffer *input[], guint **output_dims, gfloat *default_value, GError **error)
{
    UfoFilterSinoGeneratorPrivate *priv = UFO_FILTER_SINO_GENERATOR_GET_PRIVATE (filter);
    guint width, height;

    ufo_buffer_get_2d_dimensions (input[0], &width, &height);

    priv->sino_width = width;
    priv->num_sinos  = height;
    priv->projection = 1;
    priv->sinograms  = g_malloc0 (sizeof (float) * priv->num_sinos * priv->sino_width * priv->num_projections);
    priv->sino_mem_offset = width * priv->num_projections;
    priv->current_sino = 0;

    output_dims[0][0] = priv->sino_width;
    output_dims[0][1] = priv->num_projections;
}

static void
ufo_filter_sino_generator_collect (UfoFilterReduce *filter, UfoBuffer *input[], UfoBuffer *output[], GError **error)
{
    UfoFilterSinoGeneratorPrivate *priv = UFO_FILTER_SINO_GENERATOR_GET_PRIVATE (filter);
    cl_command_queue cmd_queue = ufo_filter_get_command_queue (UFO_FILTER (filter));

    const gsize row_mem_offset = priv->sino_width;
    const gsize sino_mem_offset = row_mem_offset * priv->num_projections;

    if (priv->projection > priv->num_projections) {
        g_set_error (error, UFO_FILTER_ERROR, UFO_FILTER_ERROR_NOSUCHINPUT,
                     "Received %i projections, but can only handle %i projections",
                     (gint) priv->projection, priv->num_projections);
        return;
    }

    gsize proj_index = 0;
    gsize sino_index = (priv->projection - 1) * priv->sino_width;
    gfloat *src = ufo_buffer_get_host_array (input[0], cmd_queue);

    for (guint i = 0; i < priv->num_sinos; i++) {
        memcpy (priv->sinograms + sino_index, src + proj_index, sizeof (float) * priv->sino_width);
        proj_index += row_mem_offset;
        sino_index += sino_mem_offset;
    }

    priv->projection++;
}
    
static gboolean
ufo_filter_sino_generator_reduce (UfoFilterReduce *filter, UfoBuffer *output[], GError **error)
{
    gsize sino_index;
    UfoFilterSinoGeneratorPrivate *priv = UFO_FILTER_SINO_GENERATOR_GET_PRIVATE (filter);

    if (priv->current_sino == priv->num_sinos)
        return FALSE;

    sino_index = priv->current_sino * priv->sino_mem_offset;
    ufo_buffer_set_host_array (output[0], priv->sinograms + sino_index, sizeof (float) * priv->sino_mem_offset, NULL);
    priv->current_sino++;

    return TRUE;
}

static void
ufo_filter_sino_generator_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterSinoGenerator *self = UFO_FILTER_SINO_GENERATOR (object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_NUM_PROJECTIONS:
            self->priv->num_projections = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_sino_generator_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterSinoGenerator *self = UFO_FILTER_SINO_GENERATOR (object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_NUM_PROJECTIONS:
            g_value_set_uint (value, self->priv->num_projections);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void ufo_filter_sino_generator_class_init (UfoFilterSinoGeneratorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    UfoFilterReduceClass *filter_class = UFO_FILTER_REDUCE_CLASS (klass);

    gobject_class->set_property = ufo_filter_sino_generator_set_property;
    gobject_class->get_property = ufo_filter_sino_generator_get_property;
    filter_class->initialize = ufo_filter_sino_generator_initialize;
    filter_class->collect = ufo_filter_sino_generator_collect;
    filter_class->reduce = ufo_filter_sino_generator_reduce;

    sino_generator_properties[PROP_NUM_PROJECTIONS] = 
        g_param_spec_uint ("num-projections",
            "Number of projections",
            "Number of projections corresponding to the sinogram height",
            0, 8192, 1,
            G_PARAM_READWRITE);

    g_object_class_install_property (gobject_class, PROP_NUM_PROJECTIONS, sino_generator_properties[PROP_NUM_PROJECTIONS]);

    g_type_class_add_private (gobject_class, sizeof (UfoFilterSinoGeneratorPrivate));
}

static void ufo_filter_sino_generator_init (UfoFilterSinoGenerator *self)
{
    UfoInputParameter input_params[] = {{2, UFO_FILTER_INFINITE_INPUT}};
    UfoOutputParameter output_params[] = {{2}};

    UfoFilterSinoGeneratorPrivate *priv = self->priv = UFO_FILTER_SINO_GENERATOR_GET_PRIVATE (self);
    priv->num_projections = 1;

    ufo_filter_register_inputs (UFO_FILTER (self), 1, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new (void)
{
    return g_object_new (UFO_TYPE_FILTER_SINO_GENERATOR, NULL);
}
