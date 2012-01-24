#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <math.h>
#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-filter.h"

typedef float* (*filter_setup_func)(UfoFilterFilterPrivate *priv, guint32 width);

struct _UfoFilterFilterPrivate {
    cl_kernel kernel;
    filter_setup_func filter_setup;
    float bw_cutoff;
    int bw_order;
};

GType ufo_filter_filter_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterFilter, ufo_filter_filter, UFO_TYPE_FILTER);

#define UFO_FILTER_FILTER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_FILTER, UfoFilterFilterPrivate))

enum {
    PROP_0,
    PROP_FILTER_TYPE,
    PROP_BW_CUTOFF,
    PROP_BW_ORDER,
    N_PROPERTIES
};

static GParamSpec *filter_properties[N_PROPERTIES] = { NULL, };

static void mirror_coefficients(float *filter, int width)
{
    for (int k = width/2; k < width; k += 2) {
        filter[k] = filter[width - k];
        filter[k + 1] = filter[width - k + 1];
    }
}

static float *setup_pyhst_ramp(UfoFilterFilterPrivate *priv, guint32 width)
{
    float *filter = g_malloc0(width * sizeof(float));
    const float scale = 2.0 / width / width;
    filter[0] = 0.0;
    filter[1] = 1.0 / width;

    for (int k = 1; k < width / 4; k++) {
        filter[2*k] = k*scale;
        filter[2*k + 1] = filter[2*k];
    }

    mirror_coefficients(filter, width);
    return filter;
}

static float *setup_ramp(UfoFilterFilterPrivate *priv, guint32 width)
{
    float *filter = g_malloc0(width * sizeof(float));
    float scale = 1.0 / width / 2.;

    for (int i = 0; i < width / 4; i++) {
        filter[2*i] = i * scale;
        filter[2*i+1] = i * scale;
    }

    mirror_coefficients(filter, width);
    return filter;
}

static float *setup_butterworth(UfoFilterFilterPrivate *priv, guint32 width)
{
    float *filter = g_malloc0(width * sizeof(float));
    int n_samples = width / 4; /* because width = n_samples * complex_parts * 2 */

    for (int i = 0; i < n_samples; i++) {
        float coefficient = i / ((float) n_samples); 
        filter[2*i] = coefficient * 1.0 / (1.0 + pow(coefficient / priv->bw_cutoff, 2.0 * priv->bw_order));
        filter[2*i+1] = filter[2*i];
    }

    mirror_coefficients(filter, width);
    return filter;
}

static void ufo_filter_filter_initialize(UfoFilter *filter)
{
    UfoFilterFilter *self = UFO_FILTER_FILTER(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->kernel = NULL;

    ufo_resource_manager_add_program(manager, "filter.cl", NULL, &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
        return;
    }

    self->priv->kernel = ufo_resource_manager_get_kernel(manager, "filter", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

static void ufo_filter_filter_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));

    UfoFilterFilterPrivate *priv = UFO_FILTER_FILTER_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    UfoBuffer *input = (UfoBuffer *) ufo_channel_get_input_buffer(input_channel);
    int num_dims = 0;
    int *dim_size = NULL;
    ufo_buffer_get_dimensions(input, &num_dims, &dim_size);
    ufo_channel_allocate_output_buffers(output_channel, num_dims, dim_size);
    size_t global_work_size[2] = { dim_size[0], dim_size[1] };

    float *coefficients = priv->filter_setup(priv, dim_size[0]);
    UfoBuffer *filter_buffer = ufo_resource_manager_request_buffer(manager, 1, &dim_size[0], coefficients, command_queue);
    g_free(coefficients);

    cl_mem filter_mem = (cl_mem) ufo_buffer_get_device_array(filter_buffer, command_queue);
    cl_event event;
        
    while (input != NULL) {
        UfoBuffer *output = ufo_channel_get_output_buffer(output_channel);
        cl_mem freq_out_mem = (cl_mem) ufo_buffer_get_device_array(output, command_queue);
        cl_mem freq_in_mem = (cl_mem) ufo_buffer_get_device_array(input, command_queue);

        clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), (void *) &freq_in_mem);
        clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), (void *) &freq_out_mem);
        clSetKernelArg(priv->kernel, 2, sizeof(cl_mem), (void *) &filter_mem);
        CHECK_ERROR(clEnqueueNDRangeKernel(command_queue,
                priv->kernel, 
                2, NULL, global_work_size, NULL, 
                0, NULL, &event));

        clFinish(command_queue);

        ufo_buffer_attach_event(output, event);
        ufo_channel_finalize_input_buffer(input_channel, input);
        ufo_channel_finalize_output_buffer(output_channel, output);

        input = ufo_channel_get_input_buffer(input_channel);
    }
    ufo_resource_manager_release_buffer(manager, filter_buffer);
    ufo_channel_finish(output_channel);
    g_free(dim_size);
}

static void ufo_filter_filter_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterFilterPrivate *priv = UFO_FILTER_FILTER_GET_PRIVATE(object);
    switch (property_id) {
        case PROP_FILTER_TYPE:
            {
                const char *type = g_value_get_string(value);
                if (!g_strcmp0(type, "ramp"))
                    priv->filter_setup = &setup_ramp;
                else if (!g_strcmp0(type, "butterworth"))
                    priv->filter_setup = &setup_butterworth;
            }
            break;
        case PROP_BW_CUTOFF:
            priv->bw_cutoff = (float) g_value_get_float(value);
            break;
        case PROP_BW_ORDER:
            priv->bw_order = (float) g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_filter_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterFilterPrivate *priv = UFO_FILTER_FILTER_GET_PRIVATE(object);
    switch (property_id) {
        case PROP_FILTER_TYPE:
            if (priv->filter_setup == &setup_ramp)
                g_value_set_string(value, "ramp");
            else if (priv->filter_setup == &setup_butterworth)
                g_value_set_string(value, "butterworth");
            break;
        case PROP_BW_CUTOFF:
            g_value_set_float(value, priv->bw_cutoff);
            break;
        case PROP_BW_ORDER:
            g_value_set_int(value, priv->bw_order);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_filter_class_init(UfoFilterFilterClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_filter_set_property;
    gobject_class->get_property = ufo_filter_filter_get_property;
    filter_class->initialize = ufo_filter_filter_initialize;
    filter_class->process = ufo_filter_filter_process;

    filter_properties[PROP_FILTER_TYPE] = 
        g_param_spec_string("filter-type",
            "Type of filter",
            "Type of filter (\"ramp\", \"butterworth\")",
            "ramp",
            G_PARAM_READWRITE);

    filter_properties[PROP_BW_CUTOFF] = 
        g_param_spec_float("bw-cutoff",
            "Relative cutoff frequency",
            "Relative cutoff frequency of the Butterworth filter",
            0.0, 1.0, 0.5,
            G_PARAM_READWRITE);

    filter_properties[PROP_BW_ORDER] = 
        g_param_spec_int("bw-order",
            "Order of the Butterworth filter",
            "Order of the Butterworth filter",
            2, 32, 4,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_FILTER_TYPE, filter_properties[PROP_FILTER_TYPE]);
    g_object_class_install_property(gobject_class, PROP_BW_CUTOFF, filter_properties[PROP_BW_CUTOFF]);
    g_object_class_install_property(gobject_class, PROP_BW_ORDER, filter_properties[PROP_BW_ORDER]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterFilterPrivate));
}

static void ufo_filter_filter_init(UfoFilterFilter *self)
{
    UfoFilterFilterPrivate *priv = self->priv = UFO_FILTER_FILTER_GET_PRIVATE(self);
    priv->kernel = NULL;
    priv->filter_setup = &setup_ramp;
    priv->bw_cutoff = 0.5f;
    priv->bw_order = 4;
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_FILTER, NULL);
}
