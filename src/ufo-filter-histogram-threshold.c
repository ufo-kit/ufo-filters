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
    cl_kernel hist_kernel;
};

G_DEFINE_TYPE(UfoFilterHistogramThreshold, ufo_filter_histogram_threshold, UFO_TYPE_FILTER)

#define UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD, UfoFilterHistogramThresholdPrivate))

enum {
    PROP_0,
    PROP_EXAMPLE, /* remove this or add more */
    N_PROPERTIES
};

static GParamSpec *histogram_threshold_properties[N_PROPERTIES] = { NULL, };


static void ufo_filter_histogram_threshold_initialize(UfoFilter *filter)
{
    UfoFilterHistogramThreshold *self = UFO_FILTER_HISTOGRAM_THRESHOLD(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->hist_kernel = ufo_resource_manager_get_kernel(manager, "histthreshold.cl", "histogram", &error);

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
    const gfloat range_min = 0.0f;      /* inclusive */
    const gfloat range_max = 256.0f;    /* exclusive */
    guint dimensions[2] = { num_bins, 1 };
    ufo_channel_allocate_output_buffers(output_channel, 2, dimensions);

    cl_kernel kernel = priv->hist_kernel;
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);
    size_t global_work_size = num_bins;

    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 2, sizeof(guint), &input_size));
    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 3, sizeof(gfloat), &range_min));
    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 4, sizeof(gfloat), &range_max));

    while (input != NULL) {
        output = ufo_channel_get_output_buffer(output_channel);

        cl_mem input_mem = ufo_buffer_get_device_array(input, command_queue);
        cl_mem output_mem = ufo_buffer_get_device_array(output, command_queue);

        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &input_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &output_mem));

        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue, kernel,
                1, NULL, &global_work_size, NULL,
                0, NULL, NULL));

        ufo_channel_finalize_input_buffer(input_channel, input);
        ufo_channel_finalize_output_buffer(output_channel, output);

        input = ufo_channel_get_input_buffer(input_channel);
    }

    ufo_channel_finish(output_channel);
}

static void ufo_filter_histogram_threshold_set_property(GObject *object,
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

static void ufo_filter_histogram_threshold_get_property(GObject *object,
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

static void ufo_filter_histogram_threshold_class_init(UfoFilterHistogramThresholdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_histogram_threshold_set_property;
    gobject_class->get_property = ufo_filter_histogram_threshold_get_property;
    filter_class->initialize = ufo_filter_histogram_threshold_initialize;
    filter_class->process = ufo_filter_histogram_threshold_process;

    g_type_class_add_private(gobject_class, sizeof(UfoFilterHistogramThresholdPrivate));
}

static void ufo_filter_histogram_threshold_init(UfoFilterHistogramThreshold *self)
{
    UfoFilterHistogramThresholdPrivate *priv = self->priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(self);

    /* Use this place to register your named inputs and outputs with the
     * number of dimensions that the input is accepting and the output providing */
    ufo_filter_register_input(UFO_FILTER(self), "input", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD, NULL);
}
