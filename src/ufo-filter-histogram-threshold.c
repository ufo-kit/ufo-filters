/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

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
#include "ufo-filter-histogram-threshold.h"

/**
 * SECTION:ufo-filter-histogram-threshold
 * @Short_description: Compute threshold image from histogram
 * @Title: histogramthreshold
 *
 * Detailed description.
 */

struct _UfoFilterHistogramThresholdPrivate {
    cl_kernel   thresh_kernel;
    cl_kernel   hist_kernel;
    cl_mem      histogram_mem;

    guint width;
    guint height;
    gsize num_bins;
    gfloat lower_limit;
    gfloat upper_limit;
    gfloat *histogram;
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

static void
ufo_filter_histogram_threshold_initialize(UfoFilter *filter, UfoBuffer *inputs[], guint **dims, GError **error)
{
    UfoFilterHistogramThresholdPrivate *priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_filter_get_resource_manager(filter);
    cl_int cl_error = CL_SUCCESS;
    GError *tmp_error = NULL;

    priv->hist_kernel = ufo_resource_manager_get_kernel(manager, "histthreshold.cl", "histogram", &tmp_error);
    priv->thresh_kernel = ufo_resource_manager_get_kernel(manager, "histthreshold.cl", "threshold", &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error (error, tmp_error);
        return;
    }

    ufo_buffer_get_2d_dimensions(inputs[0], &priv->width, &priv->height);
    dims[0][0] = priv->width;
    dims[0][1] = priv->height;

    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    priv->num_bins = 256;
    priv->histogram_mem = clCreateBuffer(context,
            CL_MEM_READ_WRITE,
            priv->num_bins * sizeof(float), NULL, &cl_error);
    CHECK_OPENCL_ERROR(cl_error);

    priv->histogram = g_malloc0(priv->num_bins * sizeof(gfloat));
}

static void
ufo_filter_histogram_threshold_process_gpu(UfoFilter *filter, UfoBuffer *inputs[], UfoBuffer *outputs[], GError **error)
{
    UfoFilterHistogramThresholdPrivate *priv;
    cl_command_queue cmd_queue;
    cl_mem input_mem;
    cl_mem output_mem;
    guint input_size;

    priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(filter);
    input_size = priv->width * priv->height;
    cmd_queue = ufo_filter_get_command_queue (filter);
    size_t thresh_work_size[] = { priv->width, priv->height };

    /* Build relative histogram */
    input_mem = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 0, sizeof(cl_mem), (void *) &input_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 1, sizeof(cl_mem), (void *) &priv->histogram_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 2, sizeof(guint), &input_size));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 3, sizeof(gfloat), &priv->lower_limit));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->hist_kernel, 4, sizeof(gfloat), &priv->upper_limit));

    ufo_profiler_call (ufo_filter_get_profiler (filter),
                       cmd_queue, priv->hist_kernel,
                       1, &priv->num_bins, NULL);

    /* Threshold */
    output_mem = ufo_buffer_get_device_array(outputs[0], cmd_queue);
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->thresh_kernel, 0, sizeof(cl_mem), (void *) &input_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->thresh_kernel, 1, sizeof(cl_mem), (void *) &priv->histogram_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->thresh_kernel, 2, sizeof(cl_mem), (void *) &output_mem));

    ufo_profiler_call (ufo_filter_get_profiler (filter),
                       cmd_queue, priv->thresh_kernel,
                       2, thresh_work_size, NULL);
}

static void
ufo_filter_histogram_threshold_process_cpu(UfoFilter *filter, UfoBuffer *inputs[], UfoBuffer *outputs[], GError **error)
{
    UfoFilterHistogramThresholdPrivate *priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(filter);
    cl_command_queue cmd_queue = ufo_filter_get_command_queue (filter);
    gfloat *in = ufo_buffer_get_host_array(inputs[0], cmd_queue);
    gfloat *out = ufo_buffer_get_host_array(outputs[0], cmd_queue);
    gfloat bin_width = (priv->upper_limit - priv->lower_limit) / ((gfloat) priv->num_bins);

    /* Build normal histogram */
    memset(priv->histogram, 0, priv->num_bins * sizeof(gfloat));
    gsize input_size = priv->width * priv->height;

    for (guint i = 0; i < input_size; i++) {
        guint bin = (guint) MIN(in[i] * bin_width, ((gfloat) (priv->num_bins - 1)));
        priv->histogram[bin] += 1.0f;
    }

    /* Accumulate normalized histogram */
    priv->histogram[0] /= (gfloat) input_size;

    for (guint i = 1; i < priv->num_bins; i++)
        priv->histogram[i] = priv->histogram[i-1] + (priv->histogram[i] / (gfloat) input_size);

    /* Threshold */
    for (guint i = 0; i < input_size; i++) {
        guint bin = (guint) MIN(in[i] * bin_width, ((gfloat) (priv->num_bins - 1)));
        if (priv->histogram[bin] >= 0.95)
            out[i] = in[i];
        else
            out[i] = 0.0f;
    }
}

static void
ufo_filter_histogram_threshold_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
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

static void
ufo_filter_histogram_threshold_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
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

static void
ufo_filter_histogram_threshold_finalize(GObject *object)
{
    UfoFilterHistogramThresholdPrivate *priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(object);

    CHECK_OPENCL_ERROR (clReleaseMemObject(priv->histogram_mem));
    g_free (priv->histogram);

    G_OBJECT_CLASS (ufo_filter_histogram_threshold_parent_class)->finalize (object);
}

static void
ufo_filter_histogram_threshold_class_init(UfoFilterHistogramThresholdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_histogram_threshold_set_property;
    gobject_class->get_property = ufo_filter_histogram_threshold_get_property;
    gobject_class->finalize = ufo_filter_histogram_threshold_finalize;
    filter_class->initialize = ufo_filter_histogram_threshold_initialize;
    filter_class->process_gpu = ufo_filter_histogram_threshold_process_gpu;
    filter_class->process_cpu = ufo_filter_histogram_threshold_process_cpu;

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

    for (guint prop_id = PROP_0+1; prop_id < N_PROPERTIES; prop_id++)
        g_object_class_install_property(gobject_class, prop_id, histogram_threshold_properties[prop_id]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterHistogramThresholdPrivate));
}

static void
ufo_filter_histogram_threshold_init(UfoFilterHistogramThreshold *self)
{
    UfoFilterHistogramThresholdPrivate *priv = self->priv = UFO_FILTER_HISTOGRAM_THRESHOLD_GET_PRIVATE(self);
    UfoInputParameter input_params[] = {{2, UFO_FILTER_INFINITE_INPUT}};
    UfoOutputParameter output_params[] = {{2}};

    priv->lower_limit = 0.0f;
    priv->upper_limit = 1.0f;

    ufo_filter_register_inputs (UFO_FILTER (self), 1, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD, NULL);
}
