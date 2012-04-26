#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-histogram-threshold.h"

/**
 * SECTION:ufo-filter-histogram-threshold
 * @Short_description:
 * @Title: histogramthreshold
 *
 * Detailed description.
 */

struct _UfoFilterHistogramThresholdPrivate {
    cl_kernel thresh_kernel;
    cl_kernel hist_kernel;

    gfloat lower_limit;
    gfloat upper_limit;
};

G_DEFINE_TYPE(UfoFilterHistogramThreshold, ufo_filter_histogram_threshold, UFO_TYPE_FILTER)

#define UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD, UfoFilterHistogramThresholdPrivate))

enum {
    PROP_0,
    PROP_LOWER_LIMIT,
    PROP_UPPER_LIMIT,
    N_PROPERTIES
};

static GParamSpec *histogram_threshold_properties[N_PROPERTIES] = { NULL, };


static void ufo_filter_histogram_threshold_initialize(UfoFilter *filter)
{
    UfoFilterHistogramThreshold *self = UFO_FILTER_HISTOGRAM_THRESHOLD(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->hist_kernel = ufo_resource_manager_get_kernel(manager, "histthreshold.cl", "histogram", &error);
    self->priv->thresh_kernel = ufo_resource_manager_get_kernel(manager, "histthreshold.cl", "threshold", &error);

    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_histogram_threshold_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterHistogramThresholdPrivate *priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(filter);
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);
    UfoBuffer *output = NULL;

    if (input == NULL) {
        ufo_channel_finish(output_channel);
        return;
    }

    guint width, height;
    ufo_buffer_get_2d_dimensions(input, &width, &height);

    const guint input_size = width * height;
    const guint num_bins = 256;
    guint dimensions[2] = { width, height };
    ufo_channel_allocate_output_buffers(output_channel, 2, dimensions);

    UfoResourceManager *manager = ufo_resource_manager();
    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);
    cl_int error = CL_SUCCESS;

    size_t hist_work_size = num_bins;
    size_t thresh_work_size[] = { width, height };

    cl_mem histogram_mem = clCreateBuffer(context,
            CL_MEM_READ_WRITE,
            num_bins * sizeof(float), NULL, &error);

    CHECK_OPENCL_ERROR(error);

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 2, sizeof(guint), &input_size));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 3, sizeof(gfloat), &priv->lower_limit));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 4, sizeof(gfloat), &priv->upper_limit));

    while (input != NULL) {
        cl_mem input_mem = ufo_buffer_get_device_array(input, command_queue);

        /*
         * Build relative histogram
         */
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 0, sizeof(cl_mem), (void *) &input_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 1, sizeof(cl_mem), (void *) &histogram_mem));
        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue, priv->hist_kernel,
                1, NULL, &hist_work_size, NULL,
                0, NULL, NULL));

        /*
         * Threshold
         */
        output = ufo_channel_get_output_buffer(output_channel);
        cl_mem output_mem = ufo_buffer_get_device_array(output, command_queue);

        CHECK_OPENCL_ERROR(clSetKernelArg(priv->thresh_kernel, 0, sizeof(cl_mem), (void *) &input_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->thresh_kernel, 1, sizeof(cl_mem), (void *) &histogram_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->thresh_kernel, 2, sizeof(cl_mem), (void *) &output_mem));
        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue, priv->thresh_kernel,
                2, NULL, thresh_work_size, NULL,
                0, NULL, NULL));

        ufo_channel_finalize_input_buffer(input_channel, input);
        ufo_channel_finalize_output_buffer(output_channel, output);

        input = ufo_channel_get_input_buffer(input_channel);
    }

    ufo_channel_finish(output_channel);
    CHECK_OPENCL_ERROR(clReleaseMemObject(histogram_mem)); 
}

static void ufo_filter_histogram_threshold_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterHistogramThresholdPrivate *priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_LOWER_LIMIT:
            priv->lower_limit = g_value_get_float(value);
            break;
        case PROP_UPPER_LIMIT:
            priv->upper_limit = g_value_get_float(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_histogram_threshold_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterHistogramThresholdPrivate *priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_LOWER_LIMIT:
            g_value_set_float(value, priv->lower_limit);
            break;
        case PROP_UPPER_LIMIT:
            g_value_set_float(value, priv->upper_limit);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_histogram_threshold_class_init(UfoFilterHistogramThresholdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_histogram_threshold_set_property;
    gobject_class->get_property = ufo_filter_histogram_threshold_get_property;
    filter_class->initialize = ufo_filter_histogram_threshold_initialize;
    filter_class->process = ufo_filter_histogram_threshold_process;

    histogram_threshold_properties[PROP_LOWER_LIMIT] = 
        g_param_spec_float("lower-limit",
            "Lower limit",
            "Lower limit",
            -G_MAXFLOAT, G_MAXFLOAT, 0.0,
            G_PARAM_READWRITE);

    histogram_threshold_properties[PROP_UPPER_LIMIT] = 
        g_param_spec_float("upper-limit",
            "Upper limit",
            "Upper limit",
            -G_MAXFLOAT, G_MAXFLOAT, 1.0,
            G_PARAM_READWRITE);

    for (gint prop_id = PROP_0+1; prop_id < N_PROPERTIES; prop_id++)
        g_object_class_install_property(gobject_class, prop_id, histogram_threshold_properties[prop_id]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterHistogramThresholdPrivate));
}

static void ufo_filter_histogram_threshold_init(UfoFilterHistogramThreshold *self)
{
    UfoFilterHistogramThresholdPrivate *priv = self->priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(self);

    priv->lower_limit = 0.0f;
    priv->upper_limit = 1.0f;

    ufo_filter_register_input(UFO_FILTER(self), "input", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD, NULL);
}
