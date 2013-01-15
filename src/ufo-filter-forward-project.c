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

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-forward-project.h"

/**
 * SECTION:ufo-filter-forward-project
 * @Short_description: Forward project slices
 * @Title: forwardproject
 *
 * Forward project slice data to simulate a parallel-beam detector. The output
 * is a sinogram with projections taken at angles space
 * #UfoFilterForwardProject:angle-step units apart.
 */

struct _UfoFilterForwardProjectPrivate {
    cl_kernel   kernel;
    cl_mem      slice_mem;
    gfloat      angle_step;
    guint       num_projections;
    size_t      global_work_size[2];
};

G_DEFINE_TYPE(UfoFilterForwardProject, ufo_filter_forward_project, UFO_TYPE_FILTER)

#define UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_FORWARD_PROJECT, UfoFilterForwardProjectPrivate))

enum {
    PROP_0,
    PROP_ANGLE_STEP,
    PROP_NUM_PROJECTIONS,
    N_PROPERTIES
};

static GParamSpec *forward_project_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_forward_project_initialize(UfoFilter *filter, UfoBuffer *params[], guint **dims, GError **error)
{
    UfoFilterForwardProjectPrivate *priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE (filter);
    UfoResourceManager *manager = ufo_filter_get_resource_manager (filter);
    GError *tmp_error = NULL;
    guint width, height;
    cl_context context = (cl_context) ufo_resource_manager_get_context (manager);
    cl_int errcode;

    cl_image_format image_format = {
        .image_channel_order        = CL_R,
        .image_channel_data_type    = CL_FLOAT
    };

    priv->kernel = ufo_resource_manager_get_kernel (manager, "forwardproject.cl","forwardproject", &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error (error, tmp_error);
        return;
    }

    ufo_buffer_get_2d_dimensions (params[0], &width, &height);

    priv->slice_mem = clCreateImage2D (context,
            CL_MEM_READ_ONLY, &image_format, width, height,
            0, NULL, &errcode);

    CHECK_OPENCL_ERROR (clSetKernelArg (priv->kernel, 0, sizeof(cl_mem), (void *) &priv->slice_mem));
    CHECK_OPENCL_ERROR (clSetKernelArg (priv->kernel, 2, sizeof(float), &priv->angle_step));

    priv->global_work_size[0] = dims[0][0] = width;
    priv->global_work_size[1] = dims[0][1] = priv->num_projections;
}

static void
ufo_filter_forward_project_process_gpu(UfoFilter *filter, UfoBuffer *params[], UfoBuffer *results[], GError **error)
{
    UfoFilterForwardProjectPrivate *priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(filter);
    cl_command_queue cmd_queue;
    cl_mem input_mem;
    cl_mem output_mem;

    const gsize src_origin[3] = { 0, 0, 0 };
    const gsize region[3] = { priv->global_work_size[0], priv->global_work_size[1], 1 };

    cmd_queue = ufo_filter_get_command_queue (filter);

    input_mem = (cl_mem) ufo_buffer_get_device_array(params[0], cmd_queue);
    CHECK_OPENCL_ERROR(clEnqueueCopyBufferToImage(cmd_queue,
                                           input_mem, priv->slice_mem,
                                           0, src_origin, region,
                                           0, NULL, NULL));

    output_mem = (cl_mem) ufo_buffer_get_device_array(results[0], cmd_queue);
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), (void *) &output_mem));

    ufo_profiler_call (ufo_filter_get_profiler (filter),
                       cmd_queue, priv->kernel,
                       2, priv->global_work_size, NULL);
}

static void
ufo_filter_forward_project_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterForwardProjectPrivate *priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_ANGLE_STEP:
            priv->angle_step = g_value_get_float(value);
            break;
        case PROP_NUM_PROJECTIONS:
            priv->num_projections = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_forward_project_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterForwardProjectPrivate *priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_ANGLE_STEP:
            g_value_set_float(value, priv->angle_step);
            break;
        case PROP_NUM_PROJECTIONS:
            g_value_set_uint(value, priv->num_projections);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_forward_project_finalize(GObject *object)
{
    UfoFilterForwardProjectPrivate *priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(object);

    CHECK_OPENCL_ERROR (clReleaseMemObject(priv->slice_mem));

    G_OBJECT_CLASS (ufo_filter_forward_project_parent_class)->finalize (object);
}

static void
ufo_filter_forward_project_class_init(UfoFilterForwardProjectClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);
    gobject_class->set_property = ufo_filter_forward_project_set_property;
    gobject_class->get_property = ufo_filter_forward_project_get_property;
    gobject_class->finalize = ufo_filter_forward_project_finalize;
    filter_class->initialize = ufo_filter_forward_project_initialize;
    filter_class->process_gpu = ufo_filter_forward_project_process_gpu;

    forward_project_properties[PROP_ANGLE_STEP] =
        g_param_spec_float("angle-step",
                           "Increment of angle in radians",
                           "Increment of angle in radians",
                           -4.0f * ((gfloat) G_PI),
                           +4.0f * ((gfloat) G_PI),
                           0.0f,
                           G_PARAM_READWRITE);

    forward_project_properties[PROP_NUM_PROJECTIONS] =
        g_param_spec_uint("num-projections",
                          "Number of projections",
                          "Number of projections",
                          1, 8192, 256,
                          G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_ANGLE_STEP, forward_project_properties[PROP_ANGLE_STEP]);
    g_object_class_install_property(gobject_class, PROP_NUM_PROJECTIONS, forward_project_properties[PROP_NUM_PROJECTIONS]);
    g_type_class_add_private(gobject_class, sizeof(UfoFilterForwardProjectPrivate));
}

static void
ufo_filter_forward_project_init(UfoFilterForwardProject *self)
{
    UfoInputParameter input_params[] = {{2, UFO_FILTER_INFINITE_INPUT}};
    UfoOutputParameter output_params[] = {{2}};

    self->priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(self);
    self->priv->num_projections = 256;

    ufo_filter_register_inputs (UFO_FILTER (self), 1, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_FORWARD_PROJECT, NULL);
}
