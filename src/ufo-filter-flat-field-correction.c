#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-flat-field-correction.h"

/**
 * SECTION:ufo-filter-flat-field-correction
 * @Short_description: A short description
 * @Title: A short title
 *
 * Some in-depth information
 */

struct _UfoFilterFlatFieldCorrectionPrivate {
    guint n_pixels;
};

G_DEFINE_TYPE(UfoFilterFlatFieldCorrection, ufo_filter_flat_field_correction, UFO_TYPE_FILTER)

#define UFO_FILTER_FLAT_FIELD_CORRECTION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_FLAT_FIELD_CORRECTION, UfoFilterFlatFieldCorrectionPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

static void
ufo_filter_flat_field_correction_initialize (UfoFilter *filter, UfoBuffer *input[], guint **dims, GError **error)
{
    UfoFilterFlatFieldCorrectionPrivate *priv;
    guint width;
    guint height;

    priv = UFO_FILTER_FLAT_FIELD_CORRECTION_GET_PRIVATE (filter);

    ufo_buffer_get_2d_dimensions (input[0], &width, &height);
    priv->n_pixels = width * height;

    dims[0][0] = width;
    dims[0][1] = height;
}

static void
ufo_filter_flat_field_correction_process_cpu (UfoFilter *filter, UfoBuffer *input[], UfoBuffer *output[], GError **error)
{
    UfoFilterFlatFieldCorrectionPrivate *priv;
    cl_command_queue cmd_queue;
    gfloat *proj_data;
    gfloat *dark_data;
    gfloat *flat_data;
    gfloat *out_data;

    priv = UFO_FILTER_FLAT_FIELD_CORRECTION_GET_PRIVATE (filter);
    cmd_queue = ufo_filter_get_command_queue (UFO_FILTER (filter));

    proj_data = ufo_buffer_get_host_array (input[0], cmd_queue);
    dark_data = ufo_buffer_get_host_array (input[1], cmd_queue);
    flat_data = ufo_buffer_get_host_array (input[2], cmd_queue);
    out_data = ufo_buffer_get_host_array (output[0], cmd_queue);

    for (guint i = 0; i < priv->n_pixels; i++)
        out_data[i] = (proj_data[i] - dark_data[i]) / (flat_data[i] - dark_data[i]);
}

static void
ufo_filter_flat_field_correction_class_init (UfoFilterFlatFieldCorrectionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS (klass);

    filter_class->initialize = ufo_filter_flat_field_correction_initialize;
    filter_class->process_cpu = ufo_filter_flat_field_correction_process_cpu;

    g_type_class_add_private (gobject_class, sizeof (UfoFilterFlatFieldCorrectionPrivate));
}

static void
ufo_filter_flat_field_correction_init (UfoFilterFlatFieldCorrection *self)
{
    UfoInputParameter input_params[] = {
        { 2, UFO_FILTER_INFINITE_INPUT },   /* projections */
        { 2, UFO_FILTER_INFINITE_INPUT },   /* dark field */
        { 2, UFO_FILTER_INFINITE_INPUT },   /* flat field */
    };

    UfoOutputParameter output_params[] = {{2}};

    self->priv = UFO_FILTER_FLAT_FIELD_CORRECTION_GET_PRIVATE (self);
    ufo_filter_register_inputs (UFO_FILTER (self), 3, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new (void)
{
    return g_object_new (UFO_TYPE_FILTER_FLAT_FIELD_CORRECTION, NULL);
}
