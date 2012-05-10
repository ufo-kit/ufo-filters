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

/**
 * SECTION:ufo-filter-filter
 * @Short_description: One-dimensional filtering in frequency space
 * @Title: filter
 */

typedef float* (*filter_setup_func)(UfoFilterFilterPrivate *priv, guint width);

struct _UfoFilterFilterPrivate {
    cl_kernel kernel;
    cl_mem filter_mem;
    filter_setup_func filter_setup;
    gfloat bw_cutoff;
    gfloat bw_order;
    size_t global_work_size[2];
};

G_DEFINE_TYPE(UfoFilterFilter, ufo_filter_filter, UFO_TYPE_FILTER)

#define UFO_FILTER_FILTER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_FILTER, UfoFilterFilterPrivate))

enum {
    PROP_0,
    PROP_FILTER_TYPE,
    PROP_BW_CUTOFF,
    PROP_BW_ORDER,
    N_PROPERTIES
};

static GParamSpec *filter_properties[N_PROPERTIES] = { NULL, };

static void mirror_coefficients(float *filter, guint width)
{
    for (guint k = width/2; k < width; k += 2) {
        filter[k] = filter[width - k];
        filter[k + 1] = filter[width - k + 1];
    }
}

/* static float *setup_pyhst_ramp(UfoFilterFilterPrivate *priv, guint width) */
/* { */
/*     gfloat *filter = g_malloc0(width * sizeof(float)); */
/*     const gfloat f_width = (float) width; */
/*     const gfloat scale = 2.0f / f_width / f_width; */
/*     filter[0] = 0.0f; */
/*     filter[1] = 1.0f / f_width; */

/*     for (guint k = 1; k < width / 4; k++) { */
/*         filter[2*k] = ((gfloat) k) * scale; */
/*         filter[2*k + 1] = filter[2*k]; */
/*     } */

/*     mirror_coefficients(filter, width); */
/*     return filter; */
/* } */

static float *setup_ramp(UfoFilterFilterPrivate *priv, guint width)
{
    gfloat *filter = g_malloc0(width * sizeof(float));
    gfloat scale = 0.5f / ((gfloat) width) / 2.0f;

    for (guint k = 1; k < width / 4; k++) {
        filter[2*k] = ((gfloat) k) * scale;
        filter[2*k + 1] = filter[2*k];
    }

    mirror_coefficients(filter, width);
    return filter;
}

static float *setup_butterworth(UfoFilterFilterPrivate *priv, guint width)
{
    gfloat *filter = g_malloc0(width * sizeof(float));
    guint n_samples = width / 4; /* because width = n_samples * complex_parts * 2 */

    for (guint i = 0; i < n_samples; i++) {
        gfloat coefficient = ((gfloat) i) / ((float) n_samples); 
        filter[2*i] = coefficient * 1.0f / (1.0f + ((gfloat) pow(coefficient / priv->bw_cutoff, 2.0f * priv->bw_order)));
        filter[2*i+1] = filter[2*i];
    }

    mirror_coefficients(filter, width);
    return filter;
}

static GError *ufo_filter_filter_initialize(UfoFilter *filter, UfoBuffer *params[])
{
    UfoFilterFilterPrivate *priv = UFO_FILTER_FILTER_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    priv->kernel = ufo_resource_manager_get_kernel(manager, "filter.cl", "filter", &error);

    if (error != NULL)
        return error;

    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    guint width, height;

    ufo_buffer_get_2d_dimensions(params[0], &width, &height);
    ufo_channel_allocate_output_buffers_like(output_channel, params[0]);
    priv->global_work_size[0] = width;
    priv->global_work_size[1] = height;

    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    cl_int err = CL_SUCCESS;
    float *coefficients = priv->filter_setup(priv, width);
    priv->filter_mem = clCreateBuffer(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
            width * sizeof(float), coefficients, &err);
    CHECK_OPENCL_ERROR(err);
    g_free(coefficients);
    return NULL;
}

static GError *ufo_filter_filter_process_gpu(UfoFilter *filter, 
        UfoBuffer *params[], UfoBuffer *results[], gpointer cmd_queue)
{
    UfoFilterFilterPrivate *priv = UFO_FILTER_FILTER_GET_PRIVATE(filter);
    cl_mem freq_out_mem = (cl_mem) ufo_buffer_get_device_array(results[0], (cl_command_queue) cmd_queue);
    cl_mem freq_in_mem = (cl_mem) ufo_buffer_get_device_array(params[0], (cl_command_queue) cmd_queue);

    clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), (void *) &freq_in_mem);
    clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), (void *) &freq_out_mem);
    clSetKernelArg(priv->kernel, 2, sizeof(cl_mem), (void *) &priv->filter_mem);

    CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel((cl_command_queue) cmd_queue,
                priv->kernel, 
                2, NULL, priv->global_work_size, NULL, 
                0, NULL, NULL));
    return NULL;
}

static void ufo_filter_filter_finalize(GObject *object)
{
    UfoFilterFilterPrivate *priv = UFO_FILTER_FILTER_GET_PRIVATE(object);
    clReleaseMemObject(priv->filter_mem);
    G_OBJECT_CLASS(ufo_filter_filter_parent_class)->finalize(object);
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
            priv->bw_cutoff = g_value_get_float(value);
            break;
        case PROP_BW_ORDER:
            priv->bw_order = g_value_get_float(value);
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
            g_value_set_float(value, priv->bw_order);
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
    gobject_class->finalize = ufo_filter_filter_finalize;
    filter_class->initialize = ufo_filter_filter_initialize;
    filter_class->process_gpu = ufo_filter_filter_process_gpu;

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
            0.0f, 1.0f, 0.5f,
            G_PARAM_READWRITE);

    filter_properties[PROP_BW_ORDER] = 
        g_param_spec_float("bw-order",
            "Order of the Butterworth filter",
            "Order of the Butterworth filter",
            2.0f, 32.0f, 4.0f,
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
    priv->bw_order = 4.0f;

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output0", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_FILTER, NULL);
}
