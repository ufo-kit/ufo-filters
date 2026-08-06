/*
 * Copyright (C) 2011-2015 Karlsruhe Institute of Technology
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

#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gprintf.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <config.h>
#include <common/ufo-math.h>
#include "common/ufo-addressing.h"
#include "common/ufo-scarray.h"
#include "ufo-rgba-backproject-task.h"

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRGBABackprojectTask, ufo_rgba_backproject_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
UFO_TYPE_RGBA_BACKPROJECT_TASK, UfoRGBABackprojectTaskPrivate))

struct _UfoRGBABackprojectTaskPrivate {
    // Settings
    guint burst;
    guint num_projections;
    guint x_start;
    guint x_end;
    UfoScarray *center_position_x;
    UfoScarray *center_position_z;
    UfoScarray *region;
    // OpenCL
    cl_context context;
    cl_kernel accumulate_kernel;
    cl_kernel backproject_kernel;
    cl_kernel distribute_kernel;
    cl_addressing_mode addressing_mode;
    cl_sampler sampler;
    // Internal
    UfoResources *resources;
    gsize num_slices_actual;
    gsize num_slices_processing;
    gsize generated;
    gdouble overall_angle;
    gboolean region_params_checked;
    gdouble region_start, region_stop, region_step;
    gboolean distributed;
    gsize projection_width;
    gsize projection_height;
    gsize reconstruction_side;
    // Buffers
    float *host_buffer_angles;
    cl_mem device_buffer_projections;
    cl_mem device_texture_projections;
    cl_mem device_buffer_angles;
    cl_mem device_coalesced_slices;
    cl_mem device_final_slices;
};

enum {
    PROP_0,
    PROP_BURST,
    PROP_NUM_PROJECTIONS,
    PROP_OVERALL_ANGLE,
    PROP_X_START,
    PROP_X_END,
    PROP_CENTER_POSITION_X,
    PROP_CENTER_POSITION_Z,
    PROP_REGION,
    PROP_ADDRESSING_MODE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

static gboolean
checked_mul_size (gsize a, gsize b, gsize *result)
{
    if (a != 0 && b > G_MAXSIZE / a)
        return FALSE;

    *result = a * b;
    return TRUE;
}

static gboolean
checked_add_size (gsize a, gsize b, gsize *result)
{
    if (b > G_MAXSIZE - a)
        return FALSE;

    *result = a + b;
    return TRUE;
}

static gboolean
set_opencl_error (GError **error, cl_int cl_error, const gchar *operation)
{
    if (cl_error == CL_SUCCESS)
        return FALSE;

    g_set_error (error,
                 UFO_TASK_ERROR,
                 UFO_TASK_ERROR_GET_REQUISITION,
                 "%s failed with OpenCL error %d",
                 operation,
                 cl_error);
    return TRUE;
}

UfoNode *
ufo_rgba_backproject_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_RGBA_BACKPROJECT_TASK, NULL));
}

/**
 * ufo_rgba_backproject_task_get_num_inputs:
 * 
 * @task: A #UfoTask.
 * @returns: Number of incoming projections at a time.
 *
 * Specifies the number of inputs for the task. Since we want to process each single incoming
 * projection individually, task expects a single input. Called once for the task object.
 */
static guint
ufo_rgba_backproject_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

/**
 * ufo_rgba_backproject_task_get_num_dimensions:
 * 
 * @task: A #UfoTask.
 * @input: Dimension of the input. 
 * @returns: Number of dimensions for single incoming projection.
 * 
 * Specifies the number of dimensions of the input. A single incoming projection has 2 dimensions.
 * Called once for the task object.
 */
static guint
ufo_rgba_backproject_task_get_num_dimensions (UfoTask *task, guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

/**
 * ufo_rgba_backproject_task_get_mode:
 * 
 * @task: A #UfoTask.
 * @returns: A bitwise OR of the task modes (#UfoTaskMode) that this task supports.
 * 
 * Specifies the mode in which the task operates. This task is designed to process a stream of
 * incoming projections in batches. It operates in a reductor mode, meaning it expects to receive a
 * stream of inputs and produce an output and the task is intended to run on GPU devices. Called
 * once for the task object.
 */
static UfoTaskMode
ufo_rgba_backproject_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_GPU;
}

/**
 * ufo_rgba_backproject_task_setup:
 * 
 * @task: A #UfoTask.
 * @resources: A #UfoResources instance containing the OpenCL context and kernels.
 * @error: A pointer to a #GError that will be set if an error occurs.
 * 
 * Sets up the runtime resources required for the task. It initializes the OpenCL kernels and all
 * host and device buffers whose size is known during task setup. Called once for the task.
 */
static void
ufo_rgba_backproject_task_setup (UfoTask *task, UfoResources *resources, GError **error)
{
    UfoRGBABackprojectTaskPrivate *priv = UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(task);
    // Instantiate resources.
    priv->resources = g_object_ref (resources);
    // Instantiate OpenCL resources.
    priv->context = ufo_resources_get_context(priv->resources);
    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainContext (priv->context), error);
    // Instantiate Kernels
    priv->accumulate_kernel = ufo_resources_get_kernel(priv->resources, "rgba-backproject.cl",
        "accumulate", NULL, error);
    priv->backproject_kernel = ufo_resources_get_kernel(priv->resources, "rgba-backproject.cl",
        "backproject", NULL, error);
    priv->distribute_kernel = ufo_resources_get_kernel(priv->resources, "rgba-backproject.cl",
        "distribute", NULL, error);
    if (priv->accumulate_kernel != NULL)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->accumulate_kernel), error);
    if (priv->backproject_kernel != NULL)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->backproject_kernel), error);
    if (priv->distribute_kernel != NULL)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->distribute_kernel), error);
    // Instantiate sampler
    cl_int cl_err;
    priv->sampler = clCreateSampler (
        priv->context, (cl_bool) FALSE, priv->addressing_mode, CL_FILTER_LINEAR, &cl_err);
    if (cl_err != CL_SUCCESS) {
        g_set_error (error,
                     UFO_TASK_ERROR,
                     UFO_TASK_ERROR_SETUP,
                     "creating projection sampler failed with OpenCL error %d",
                     cl_err);
        return;
    }
    // Allocate one interleaved host-side buffer for cosine and sine components.
    if (!priv->num_projections) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "number of projections not set");
        return;
    }
    priv->host_buffer_angles = g_try_new0 (float, 2 * priv->num_projections);
    if (priv->host_buffer_angles == NULL) {
        g_set_error_literal (error,
                             UFO_TASK_ERROR,
                             UFO_TASK_ERROR_SETUP,
                             "allocating host angle lookup tables failed");
        return;
    }
    const float ang_delta = priv->overall_angle / (float) priv->num_projections;
    for (uint32_t theta = 0; theta < priv->num_projections; theta++) {
        priv->host_buffer_angles[2 * theta] = (float) cosf(theta * ang_delta);
        priv->host_buffer_angles[2 * theta + 1] = (float) sinf(theta * ang_delta);
    }
    // Allocate one device-side buffer matching an OpenCL float2 array.
    cl_int cl_error;
    if (!priv->device_buffer_angles) {
        priv->device_buffer_angles = clCreateBuffer(priv->context, CL_MEM_READ_ONLY,
            2 * priv->burst * sizeof(float), NULL, &cl_error);
        if (cl_error != CL_SUCCESS) {
            g_set_error (error,
                         UFO_TASK_ERROR,
                         UFO_TASK_ERROR_SETUP,
                         "allocating angle lookup table failed with OpenCL error %d",
                         cl_error);
            return;
        }
    }
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "burst size: %u", priv->burst);
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "number of projections: %u", priv->num_projections);
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "overall angle: %f", priv->overall_angle);
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "center position-x: %f",
        (cl_float) ufo_scarray_get_double(priv->center_position_x, 0));
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "addressing mode: %u", priv->addressing_mode);
}

/**
 * ufo_rgba_backproject_task_requisition:
 * 
 * @task: A #UfoTask.
 * @inputs: Input #UfoBuffer resources. Number of buffers depends on `get_num_inputs` function.
 * @requisition: A #UfoRequisition to describe the dimensions of the O/P data.
 * @error: A pointer to a #GError that will be set if an error occurs.
 * 
 * Called for each iteration of the task right before `process` function to specify the O/P size
 * for the given I/P size. It initializes those device-side resources whose size would be known
 * in runtime only after the task execution starts.
 * 
 * NOTE: Conventionally, this function deals with input and output requisitions. A #UfoRequisition
 * object captures the dimensionality information for the input and output for the task. Input
 * requisition `in_req` is inferred from the input buffer (since we specified number of input as 1
 * we expect the `inputs` array to have a single item). Output `requisition`, which this function
 * receives as a parameter, is initialized according to the input requisition `in_req`. Output
 * `requisition` initialized here becomes relevant, when we process output slices in generate
 * function, because output buffers are initialized accordingly.
 */
static void
ufo_rgba_backproject_task_get_requisition (UfoTask *task, UfoBuffer **inputs,
    UfoRequisition *requisition, GError **error)
{
    UfoRGBABackprojectTaskPrivate *priv = UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(task);
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    UfoRequisition in_req;
    ufo_buffer_get_requisition(inputs[0], &in_req);

    if (priv->projection_width != 0 &&
        (priv->projection_width != in_req.dims[0] || priv->projection_height != in_req.dims[1])) {
        g_set_error_literal (error,
                             UFO_TASK_ERROR,
                             UFO_TASK_ERROR_GET_REQUISITION,
                             "projection dimensions changed after RGBA backprojection resources were allocated");
        return;
    }

    if (in_req.dims[0] > G_MAXUINT) {
        g_set_error_literal (error,
                             UFO_TASK_ERROR,
                             UFO_TASK_ERROR_GET_REQUISITION,
                             "projection width exceeds the supported x-coordinate range");
        return;
    }

    guint resolved_x_end = priv->x_end == 0 ? (guint) in_req.dims[0] : priv->x_end;
    if (priv->x_start >= resolved_x_end || resolved_x_end > in_req.dims[0]) {
        g_set_error (error,
                     UFO_TASK_ERROR,
                     UFO_TASK_ERROR_GET_REQUISITION,
                     "x region [%u, %u) must be non-empty and lie within projection width %zu",
                     priv->x_start,
                     resolved_x_end,
                     in_req.dims[0]);
        return;
    }

    gsize reconstruction_side = resolved_x_end - priv->x_start;
    if (priv->reconstruction_side != 0 && priv->reconstruction_side != reconstruction_side) {
        g_set_error_literal (error,
                             UFO_TASK_ERROR,
                             UFO_TASK_ERROR_GET_REQUISITION,
                             "x region changed after RGBA backprojection resources were allocated");
        return;
    }

    // The x interval is half-open and applies to both in-slice coordinates, yielding square slices.
    requisition->n_dims = 2;
    requisition->dims[0] = reconstruction_side;
    requisition->dims[1] = reconstruction_side;
    // Check region parameters to determine number of slices to be processed and produced along
    // with the feasibility of device memory allocation.
    // Parameter center_position_z specifies a reference point for region parameter. Region parameter
    // consists of [start, stop, step] among which start and stop can be -ve or +ve but step must
    // always be +ve. Moreover stop must be greater than start. Using center_position_z, start and
    // stop we translate the actual z positions of starting and ending slices to be reconstructed
    // with respect to the actual height of the projection. Derives z positions must be within the
    // bound of actual height of the projection.
    if (!priv->region_params_checked) {
        gdouble _region_start, _region_stop, _region_step, _center_position_z;
        _center_position_z = ufo_scarray_get_double(priv->center_position_z, 0);
        if (UFO_MATH_ARE_ALMOST_EQUAL (ufo_scarray_get_double (priv->region, 2), 0)) {
            _region_start = 0.0f;
            _region_stop = 1.0f;
            _region_step = 1.0f;
        } else {
            _region_start = ufo_scarray_get_double(priv->region, 0);
            _region_stop = ufo_scarray_get_double(priv->region, 1);
            _region_step = ufo_scarray_get_double(priv->region, 2);
        }
        if ((cl_int) _region_stop <= (cl_int) _region_start || (cl_int) _region_step <= 0) {
            g_set_error_literal (
                error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                "region start has to be less than region stop and region step has to be positive");
            return;
        }
        _region_start += _center_position_z;
        _region_stop += _center_position_z;
        if (_region_start < 0.0 || _region_stop > (gdouble) in_req.dims[1]) {
            g_set_error_literal (
                error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                "specified slice region is out of bound");
            return;
        }
        priv->region_start = _region_start;
        priv->region_stop = _region_stop;
        priv->region_step = _region_step;
        g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "center position-z: %u", (cl_int) _center_position_z);
        g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "region to be reconstructed: [start=%g, stop=%g, step=%g]",
            priv->region_start, priv->region_stop, priv->region_step);
        priv->num_slices_actual = (gsize) ceil((priv->region_stop - priv->region_start) / priv->region_step);
        priv->num_slices_processing = (gsize)(ceil((gdouble) priv->num_slices_actual / (gdouble) 4) * 4);
        if (priv->num_slices_actual == 0) {
            g_set_error_literal (error,
                                 UFO_TASK_ERROR,
                                 UFO_TASK_ERROR_GET_REQUISITION,
                                 "specified slice region does not contain any slices");
            return;
        }

        gsize projection_pixels = 0, projections_size = 0, texture_texels = 0, texture_size = 0;
        gsize slice_pixels = 0, volume_values = 0, volume_size = 0, lut_size = 0;
        gsize total_size = 0, next_total = 0;
        gboolean sizes_valid =
            checked_mul_size (in_req.dims[0], in_req.dims[1], &projection_pixels) &&
            checked_mul_size (projection_pixels, priv->burst, &projections_size) &&
            checked_mul_size (projections_size, sizeof (cl_float), &projections_size) &&
            checked_mul_size (in_req.dims[0], priv->num_slices_processing / 4, &texture_texels) &&
            checked_mul_size (texture_texels, priv->burst, &texture_texels) &&
            checked_mul_size (texture_texels, 4 * sizeof (cl_half), &texture_size) &&
            checked_mul_size (reconstruction_side, reconstruction_side, &slice_pixels) &&
            checked_mul_size (slice_pixels, priv->num_slices_processing, &volume_values) &&
            checked_mul_size (volume_values, sizeof (cl_float), &volume_size) &&
            checked_mul_size (priv->burst, 2 * sizeof (cl_float), &lut_size) &&
            checked_add_size (total_size, projections_size, &next_total);
        total_size = next_total;
        sizes_valid = sizes_valid && checked_add_size (total_size, texture_size, &next_total);
        total_size = next_total;
        sizes_valid = sizes_valid && checked_add_size (total_size, volume_size, &next_total);
        total_size = next_total;
        sizes_valid = sizes_valid && checked_add_size (total_size, volume_size, &next_total);
        total_size = next_total;
        sizes_valid = sizes_valid && checked_add_size (total_size, lut_size, &next_total);
        total_size = next_total;

        if (!sizes_valid) {
            g_set_error_literal (error,
                                 UFO_TASK_ERROR,
                                 UFO_TASK_ERROR_GET_REQUISITION,
                                 "RGBA backprojection buffer size calculation overflowed");
            return;
        }

        GValue *max_mem_alloc_size_gvalue = ufo_gpu_node_get_info (node, UFO_GPU_NODE_INFO_MAX_MEM_ALLOC_SIZE);
        GValue *global_mem_size_gvalue = ufo_gpu_node_get_info (node, UFO_GPU_NODE_INFO_GLOBAL_MEM_SIZE);
        // Even if a card claims to be able to allocate more than 4 GB (e.g. RTX* 8000) we get OpenCL
        // errors, so limit it to 4 GB.
        cl_ulong max_mem_alloc_size = MIN (
            g_value_get_ulong (max_mem_alloc_size_gvalue), ((cl_ulong) 1) << 32);
        cl_ulong global_mem_size = g_value_get_ulong (global_mem_size_gvalue);
        g_value_unset (max_mem_alloc_size_gvalue);
        g_value_unset (global_mem_size_gvalue);
        g_free (max_mem_alloc_size_gvalue);
        g_free (global_mem_size_gvalue);

        if (projections_size > max_mem_alloc_size || texture_size > max_mem_alloc_size ||
            volume_size > max_mem_alloc_size || lut_size > max_mem_alloc_size) {
            g_set_error (error,
                         UFO_TASK_ERROR,
                         UFO_TASK_ERROR_GET_REQUISITION,
                         "an RGBA backprojection allocation exceeds the device limit of %" G_GUINT64_FORMAT " bytes",
                         (guint64) max_mem_alloc_size);
            return;
        }
        if (total_size > global_mem_size) {
            g_set_error (error,
                         UFO_TASK_ERROR,
                         UFO_TASK_ERROR_GET_REQUISITION,
                         "RGBA backprojection requires %zu bytes but the device has %" G_GUINT64_FORMAT " bytes",
                         total_size,
                         (guint64) global_mem_size);
            return;
        }

        cl_command_queue cmd_queue = ufo_gpu_node_get_cmd_queue (node);
        cl_device_id device;
        size_t max_image_width, max_image_height, max_image_layers;
        cl_int cl_error = clGetCommandQueueInfo (cmd_queue,
                                                  CL_QUEUE_DEVICE,
                                                  sizeof (cl_device_id),
                                                  &device,
                                                  NULL);
        if (set_opencl_error (error, cl_error, "querying the OpenCL device"))
            return;
        cl_error = clGetDeviceInfo (device, CL_DEVICE_IMAGE2D_MAX_WIDTH,
                                    sizeof (size_t), &max_image_width, NULL);
        if (set_opencl_error (error, cl_error, "querying maximum image width"))
            return;
        cl_error = clGetDeviceInfo (device, CL_DEVICE_IMAGE2D_MAX_HEIGHT,
                                    sizeof (size_t), &max_image_height, NULL);
        if (set_opencl_error (error, cl_error, "querying maximum image height"))
            return;
        cl_error = clGetDeviceInfo (device, CL_DEVICE_IMAGE_MAX_ARRAY_SIZE,
                                    sizeof (size_t), &max_image_layers, NULL);
        if (set_opencl_error (error, cl_error, "querying maximum image array size"))
            return;
        if (in_req.dims[0] > max_image_width ||
            priv->num_slices_processing / 4 > max_image_height ||
            priv->burst > max_image_layers) {
            g_set_error (error,
                         UFO_TASK_ERROR,
                         UFO_TASK_ERROR_GET_REQUISITION,
                         "projection texture %zux%zux%u exceeds device image limits %zux%zux%zu",
                         in_req.dims[0],
                         priv->num_slices_processing / 4,
                         priv->burst,
                         max_image_width,
                         max_image_height,
                         max_image_layers);
            return;
        }

        g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "#slices processed: %lu", priv->num_slices_processing);
        g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "#slices produced: %lu", priv->num_slices_actual);
        g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "x region: [%u, %u), side: %zu",
               priv->x_start, resolved_x_end, reconstruction_side);
        priv->projection_width = in_req.dims[0];
        priv->projection_height = in_req.dims[1];
        priv->reconstruction_side = reconstruction_side;
        priv->region_params_checked = TRUE;
    }
    // Allocate device side ring-buffer and additional resources using requisitions. Projection
    // ring buffer contains burst number of full projections as the region stride is handled by
    // accumulate kernel.
    cl_int cl_error;
    if (!priv->device_buffer_projections) {
        gsize projection_buffer_size = priv->burst * priv->projection_width *
                                       priv->projection_height * sizeof (cl_float);
        priv->device_buffer_projections = clCreateBuffer(
            priv->context,
            CL_MEM_READ_ONLY,
            projection_buffer_size,
            NULL,
            &cl_error);
        if (set_opencl_error (error, cl_error, "allocating the projection ring buffer"))
            return;
    }
    if (!priv->device_texture_projections) {
        cl_image_format fmt = {CL_RGBA, CL_HALF_FLOAT};
        cl_image_desc desc = {0};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D_ARRAY;
        desc.image_width = in_req.dims[0];
        desc.image_height = priv->num_slices_processing / 4;
        desc.image_depth = 0;
        desc.image_array_size = priv->burst;
        priv->device_texture_projections = clCreateImage(priv->context, CL_MEM_READ_WRITE,
            &fmt, &desc, NULL, &cl_error);
        if (set_opencl_error (error, cl_error, "allocating the projection texture"))
            return;
    }
    if (!priv->device_coalesced_slices) {
        size_t coal_slice_size = requisition->dims[0] * requisition->dims[1] * (
            priv->num_slices_processing / 4) * sizeof(cl_float4);
        priv->device_coalesced_slices = clCreateBuffer(priv->context, CL_MEM_READ_WRITE,
            coal_slice_size, NULL, &cl_error);
        if (set_opencl_error (error, cl_error, "allocating the coalesced slice buffer"))
            return;
    }
    if (!priv->device_final_slices) {
        size_t final_slice_size = requisition->dims[0] * requisition->dims[1] *
                                  priv->num_slices_processing * sizeof (cl_float);
        priv->device_final_slices = clCreateBuffer (priv->context,
                                                     CL_MEM_WRITE_ONLY,
                                                     final_slice_size,
                                                     NULL,
                                                     &cl_error);
        if (set_opencl_error (error, cl_error, "allocating the final slice buffer"))
            return;
    }
}

/**
 * ufo_rgba_backproject_task_process:
 * 
 * @task: A #UfoTask.
 * @inputs: Input #UfoBuffer resources. Number of buffers depends on `get_num_inputs` function.
 * @output: A #UfoBuffer having the output if any. Output buffer may not be used at this stage,
 * which is especially true since this task operates in UFO_TASK_MODE_REDUCTOR, hence output of the
 * task would take place in generate function.
 * @requisition: A #UfoRequisition to describe the dimensions of the O/P data.
 * @returns: TRUE until `process` should be called iteratively. FALSE marks the end of processing.
 * 
 * Called for each iteration of the task to process individual projections.
 * 
 * NOTE: To process incoming stream of projections in batches the function determines whether we are
 * at a COMPLETE or INCOMPLETE burst (batch) scenario. COMPLETE burst means we have sufficient
 * number of projections left to process out of total and therefore next kernel execution can happen
 * over configured `priv->burst` projections. In contrast, INCOMPLETE burst means that we are
 * approaching the end of processing and less then `priv->burst` projections are left to process.
 * Following variables helps in distinguishing between two scenarios.
 * 
 * - `processed_proj_count`: Number of processed inputs/projections (tracked by ufo task node using
 * `num_processed` variable internally) at any given point during execution.
 * 
 * - `actual_burst`: Actual number of projections that we can process with next kernel execution,
 * means this is our runtime batch in practice. In COMPLETE burst scenario `actual_burst` is equal
 * to `priv->burst`. In INCOMPLETE burst scenario `actual_burst` is lesser than `priv->burst`.
 * 
 * - `idx_actual_burst`: Index of current projection in actual burst. Modulo operator is used with
 * `actual_burst` to compute this and prevent going out of bounds, therefore it ranges in
 * [0, `actual_burst`).
 * 
 * Both the actual burst size and projection index in actual burst are needed to correctly update
 * the ring buffer.
 * 
 * INCOMPLETE Burst: `(processed_proj_count >= (priv->num_projections / priv->burst) * priv->burst)`
 * 
 * Expression `(priv->num_projections / priv->burst) * priv->burst` tells us total number of
 * projections we can process with configured `priv->burst`. If our current `processed_proj_count`
 * exceeds that number it means we have less then `priv->burst` projections left to process out of
 * `priv->num_projections`. In that case actual_burst would be remainder number of projections after
 * processing all the complete bursts and this is then our current `actual_burst`.
 * Next we calculate index of the current projection inside its own burst, `idx_actual_burst` by
 * calculating the offset between current processed projection count and total number of projections
 * that can be processed with complete burst. There are three specific details, that we want to keep
 * in mind in this regard.
 * 
 * 1. `(priv->num_projections / priv->burst)` is an integer division and it yields the total number
 * of complete bursts possible for `priv->num_projections` with burst size `priv->burst` and
 * subsequently when `priv->burst` is multiplied to that we get the total number of projections
 * covered by the complete bursts.
 * 
 * 2. `(processed_proj_count - (priv->num_projections / priv->burst) * priv->burst)` yields the
 * offset between the 'current projection being processed' and 'total number of projections covered
 * by the complete bursts'. Former is greater or equal to latter (our INCOMPLETE burst condition
 * above). It means this control flow is only active when we are just processing
 * `(priv->num_projections / priv->burst) * priv->burst]'th` projection or exceeded that number.
 * Hence, this offset can be 0, 1, 2... and so on and when we perform modulo operation on that with
 * `actual_burst` we get the desired index. This modulo operation acts as a guard that this index
 * will never be out of bounds of the `actual_burst` size.
 * 
 * 3. `actual_burst != 0` safe-guards against division by zero problem. It is possible to have a
 * situation, when all the projections can be processed using complete bursts only (e.g., 3000 % 24
 * = 0). If that's the case expression `priv->num_projections % priv->burst` will compute
 * `actual_burst` as 0 and we might land into division by zero situation.
 * 
 * COMPLETE Burst: `(processed_proj_count < (priv->num_projections / priv->burst) * priv->burst)`
 * 
 * In this simpler scenario `actual_burst` would be the configured `priv->burst` because we have
 * sufficient number of projections still left to process such that next kernel execution can happen
 * for `priv->burst` projections. So we can directly set `actual_burst` accordingly. We calculate
 * the `idx_actual_burst` using modulo operation of `processed_proj_count` by `actual_burst`, which
 * calculates value in range [0, `actual_burst`). Crucial difference here is the `actual_burst`.
 * In this scenario it is the configured size and in other case it is derived from the number of
 * projections left to process.
 * 
 */
static gboolean
ufo_rgba_backproject_task_process (UfoTask *task, UfoBuffer **inputs, UfoBuffer *output,
    UfoRequisition *requisition)
{
    UfoRGBABackprojectTaskPrivate *priv = UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(task);
    UfoRequisition in_req;
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cl_command_queue cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    UfoProfiler *profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_buffer_get_requisition (inputs[0], &in_req);
    guint idx_actual_burst, processed_proj_count, actual_burst;
    g_object_get (task, "num_processed", &processed_proj_count, NULL);
    if (processed_proj_count >= (priv->num_projections / priv->burst) * priv->burst) {
        // Scenario: INCOMPLETE Burst (means we have processed all the projections which we could
        // process with complete bursts and number of projections left to process would not make a
        // a complete burst)
        actual_burst = priv->num_projections % priv->burst;
        idx_actual_burst = actual_burst != 0 ? (processed_proj_count - (
            priv->num_projections / priv->burst) * priv->burst) % actual_burst : 0;
    } else {
        // Scenario: COMPLETE Burst (means we have not yet processed all the projections which can
        // be processed with complete bursts, in other words we can still have a complete burst)
        actual_burst = priv->burst;
        idx_actual_burst = processed_proj_count % actual_burst;
    }
    // The ring-buffer slot is one complete width-by-height projection. Host-resident inputs are
    // uploaded directly into that slot; device-resident inputs remain on the GPU and are copied
    // device-to-device.
    gsize projection_size = in_req.dims[0] * in_req.dims[1] * sizeof (cl_float);
    gsize projection_offset = idx_actual_burst * projection_size;
    UfoBufferLocation input_location = ufo_buffer_get_location (inputs[0]);

    if (input_location == UFO_BUFFER_LOCATION_HOST) {
        float *curr_proj_array = ufo_buffer_get_host_array (inputs[0], cmd_queue);
        UFO_RESOURCES_CHECK_CLERR (
            clEnqueueWriteBuffer (cmd_queue,
                                  priv->device_buffer_projections,
                                  CL_TRUE,
                                  projection_offset,
                                  projection_size,
                                  curr_proj_array,
                                  0, NULL, NULL));
    }
    else {
        cl_mem curr_proj_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
        UFO_RESOURCES_CHECK_CLERR (
            clEnqueueCopyBuffer (cmd_queue,
                                 curr_proj_mem,
                                 priv->device_buffer_projections,
                                 0,
                                 projection_offset,
                                 projection_size,
                                 0, NULL, NULL));
    }
    // Dispatch kernels, once burst is ready. Since `idx_actual_burst` is the index of the current
    // projection in its burst, if (idx_actual_burst + 1) is equal to the derived burst size we can
    // process the batch.
    if (idx_actual_burst + 1 == actual_burst) {
        // We compute the global index of the first projection to be processed in the current
        // burst. We need it to copy the rotation coefficients from the host side buffer to device
        // side. Incrementing global index of the processed projections so far by 1 gives us the
        // global index of the current projection and since we want to process `actual_burst` number
        // of projections in next kernel execution subtracting it from the global index of current
        // projection gives us the index we want.
        cl_uint global_proj_idx = (cl_uint) (processed_proj_count + 1 - actual_burst);
        g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "processing %u projections starting from %u", actual_burst,
            global_proj_idx);
        /// STAGE: ACCUMULATE (Packs four rows of the projection into one using RGBA format)
        cl_int row_start = (cl_int) priv->region_start;
        cl_int row_step = (cl_int) priv->region_step;
        cl_int projection_height = (cl_int) in_req.dims[1];
        cl_int projection_width = (cl_int) in_req.dims[0];
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 0, sizeof(cl_mem),
        &priv->device_buffer_projections));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 1, sizeof(cl_mem),
        &priv->device_texture_projections));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 2, sizeof(cl_int),
        &row_start));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 3, sizeof(cl_int),
        &row_step));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 4, sizeof(cl_int),
        &projection_height));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 5, sizeof(cl_int),
        &projection_width));
        const size_t accumulate_work_size[] = {in_req.dims[0], priv->num_slices_processing / 4, actual_burst};
        ufo_profiler_call (profiler, cmd_queue, priv->accumulate_kernel, 3,
            accumulate_work_size, NULL);
        /// STAGE: BACKPROJECT
        UFO_RESOURCES_CHECK_CLERR (clEnqueueWriteBuffer (
            cmd_queue, priv->device_buffer_angles, CL_FALSE, 0,
            2 * actual_burst * sizeof(float),
            priv->host_buffer_angles + 2 * global_proj_idx,
            0, NULL, NULL));
        const size_t bp_work_size[] = {
            requisition->dims[0], requisition->dims[1], priv->num_slices_processing / 4};
        const cl_float center_position_x = (cl_float) ufo_scarray_get_double(priv->center_position_x, 0);
        const cl_int slice_width = (cl_int) requisition->dims[0];
        const cl_int slice_height = (cl_int) requisition->dims[1];
        const cl_uint x_start = priv->x_start;
        const cl_uint first_burst = global_proj_idx == 0;
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 0, sizeof(cl_mem),
        &priv->device_texture_projections));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 1, sizeof(cl_mem),
        &priv->device_coalesced_slices));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 2, sizeof(cl_mem),
        &priv->device_buffer_angles));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 3, sizeof(cl_float),
        &center_position_x));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 4, sizeof(cl_uint),
        &actual_burst));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 5, sizeof(cl_sampler),
        &priv->sampler));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 6, sizeof(cl_int),
        &slice_width));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 7, sizeof(cl_int),
        &slice_height));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 8, sizeof(cl_uint),
        &x_start));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 9, sizeof(cl_uint),
        &first_burst));
        ufo_profiler_call_blocking (profiler, cmd_queue, priv->backproject_kernel, 3, bp_work_size,
            NULL);
    }
    return TRUE;
}

/**
 * ufo_rgba_backproject_task_generate:
 * 
 * @task: A #UfoTask.
 * @output: A #UfoBuffer to contain the output from the task.
 * @requisition: A #UfoRequisition to describe the dimensions of the output data.
 * @returns: TRUE until `generate` should be called iteratively. FALSE marks the end of generating.
 * 
 * Called at the end of all `process` iterations to generate the output from the task. This method
 * call will be repeated until it returns TRUE, means we still have slices to generate. At the end
 * of producing all slices it will return FALSE, which marks the end of generate. Output requisition
 * initialized during `_get_requisition` function earlier becomes relevant here when we request for
 * device memory to copy the generated slice from the buffer.
 */
static gboolean
ufo_rgba_backproject_task_generate (UfoTask *task, UfoBuffer *output, UfoRequisition *requisition)
{
    UfoRGBABackprojectTaskPrivate *priv = UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(task);
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    UfoProfiler *profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    cl_command_queue cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    guint processed_proj_count;
    g_object_get (task, "num_processed", &processed_proj_count, NULL);
    if (processed_proj_count < priv->num_projections) {
        g_warning ("rgba-backproject received only %u projections out of %u "
                   "specified, no output will be generated", processed_proj_count,
                   priv->num_projections);
        return FALSE;
    }
    if (priv->generated >= priv->num_slices_actual)
        return FALSE;

    /// STAGE: DISTRIBUTE (Spread the values packed into float4 buffer into separate slices)
    // Make sure that distributed is not called for each generate call.
    if (!priv->distributed) {
        const size_t dist_work_size[] = {
            requisition->dims[0], requisition->dims[1], priv->num_slices_processing / 4};
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->distribute_kernel, 0, sizeof(cl_mem),
        &priv->device_coalesced_slices));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->distribute_kernel, 1, sizeof(cl_mem),
        &priv->device_final_slices));
        const cl_int slice_width = (cl_int) requisition->dims[0];
        const cl_int slice_height = (cl_int) requisition->dims[1];
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->distribute_kernel, 2, sizeof(cl_int),
        &slice_width));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->distribute_kernel, 3, sizeof(cl_int),
        &slice_height));
        ufo_profiler_call_blocking (profiler, cmd_queue, priv->distribute_kernel, 3, dist_work_size,
            NULL);
        priv->distributed = TRUE;
    }
    /// STAGE: OUTPUT
    cl_mem out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    /// NOTE: We copy each slice from the global slice buffer to the output buffer.
    // row_pitch => size in bytes for a row of the slice.
    // slice_pitch => size in bytes for the slice (#rows * row_size)
    // src_origin => offset in bytes from the start in source buffer. Since we specified size of a
    // slice in bytes we only need to provide the depth offset.
    // dst_origin => offset in bytes from the start in destination buffer, no offset needed since we
    // want to produce 2D slices.
    // region => row width (in bytes), height (#rows), and depth (slices) of region to copy. We copy
    // each slice, hence depth is 1.
    size_t row_pitch = requisition->dims[0] * sizeof(float);
    size_t slice_pitch = requisition->dims[1] * row_pitch;
    size_t src_origin[3] = {0, 0, priv->generated};
    size_t dst_origin[3] = {0, 0, 0};
    size_t region[3] = {row_pitch, requisition->dims[1], 1};
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "generating slice %lu", priv->generated + 1);
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "src_origin: %lu %lu %lu", src_origin[0], src_origin[1],
        src_origin[2]);
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "region: %lu %lu %lu", region[0], region[1], region[2]);
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "row pitch %lu, slice pitch %lu", row_pitch, slice_pitch);
    UFO_RESOURCES_CHECK_CLERR (clEnqueueCopyBufferRect (cmd_queue,
                                                        priv->device_final_slices, out_mem,
                                                        src_origin, dst_origin, region,
                                                        row_pitch, slice_pitch,
                                                        row_pitch, 0, 0, NULL, NULL));
    priv->generated++;
    return TRUE;
}

static void
ufo_rgba_backproject_task_set_property (GObject *object, guint property_id, const GValue *value,
    GParamSpec *pspec)
{
    UfoRGBABackprojectTaskPrivate *priv = UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_BURST:
            priv->burst = g_value_get_uint(value);
            break;
        case PROP_NUM_PROJECTIONS:
            priv->num_projections = g_value_get_uint(value);
            break;
        case PROP_OVERALL_ANGLE:
            priv->overall_angle = g_value_get_double (value);
            break;
        case PROP_X_START:
            priv->x_start = g_value_get_uint (value);
            break;
        case PROP_X_END:
            priv->x_end = g_value_get_uint (value);
            break;
        case PROP_CENTER_POSITION_X:
            ufo_scarray_get_value (priv->center_position_x, value);
            break;
        case PROP_CENTER_POSITION_Z:
            ufo_scarray_get_value (priv->center_position_z, value);
            break;
        case PROP_REGION:
            ufo_scarray_get_value(priv->region, value);
            break;
        case PROP_ADDRESSING_MODE:
            priv->addressing_mode = g_value_get_enum (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_rgba_backproject_task_get_property (GObject *object, guint property_id, GValue *value,
    GParamSpec *pspec)
{
    UfoRGBABackprojectTaskPrivate *priv = UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_BURST:
            g_value_set_uint(value, priv->burst);
            break;
        case PROP_NUM_PROJECTIONS:
            g_value_set_uint(value, priv->num_projections);
            break;
        case PROP_OVERALL_ANGLE:
            g_value_set_double (value, priv->overall_angle);
            break;
        case PROP_X_START:
            g_value_set_uint (value, priv->x_start);
            break;
        case PROP_X_END:
            g_value_set_uint (value, priv->x_end);
            break;
        case PROP_CENTER_POSITION_X:
            ufo_scarray_set_value (priv->center_position_x, value);
            break;
        case PROP_CENTER_POSITION_Z:
            ufo_scarray_set_value (priv->center_position_z, value);
            break;
        case PROP_REGION:
            ufo_scarray_set_value(priv->region, value);
            break;
        case PROP_ADDRESSING_MODE:
            g_value_set_enum (value, priv->addressing_mode);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_rgba_backproject_task_finalize (GObject *object)
{
    UfoRGBABackprojectTaskPrivate *priv = UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(object);
    if (priv->device_buffer_projections) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_buffer_projections));
        priv->device_buffer_projections = NULL;
    }
    if (priv->device_texture_projections) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_texture_projections));
        priv->device_texture_projections = NULL;
    }
    if (priv->device_buffer_angles) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_buffer_angles));
        priv->device_buffer_angles = NULL;
    }
    if (priv->device_coalesced_slices) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_coalesced_slices));
        priv->device_coalesced_slices = NULL;
    }
    if (priv->device_final_slices) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_final_slices));
        priv->device_final_slices = NULL;
    }
    if (priv->accumulate_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->accumulate_kernel));
        priv->accumulate_kernel = NULL;
    }
    if (priv->backproject_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->backproject_kernel));
        priv->backproject_kernel = NULL;
    }
    if (priv->distribute_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->distribute_kernel));
        priv->distribute_kernel = NULL;
    }
    if (priv->sampler) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseSampler (priv->sampler));
        priv->sampler = NULL;
    }
    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }
    if (priv->resources) {
        g_object_unref (priv->resources);
        priv->resources = NULL;
    }
    if (priv->region) {
        ufo_scarray_free(priv->region);
        priv->region = NULL;
    }
    if (priv->center_position_x) {
        ufo_scarray_free (priv->center_position_x);
        priv->center_position_x = NULL;
    }
    if (priv->center_position_z) {
        ufo_scarray_free (priv->center_position_z);
        priv->center_position_z = NULL;
    }
    if (priv->host_buffer_angles) {
        g_free(priv->host_buffer_angles);
        priv->host_buffer_angles = NULL;
    }
    G_OBJECT_CLASS(ufo_rgba_backproject_task_parent_class)->finalize(object);
    g_log ("rgba_bp", G_LOG_LEVEL_DEBUG, "finalize: resources deallocated");
}

/**
 * Following initialization functions,
 * * ufo_task_interface_init: points
 * * ufo_rgba_backproject_task_class_init
 * * ufo_rgba_backproject_task_init
 * * serve mainly two different purposes,
 * 1) point to concrete implementations of virtual functions defined in the interface UfoTaskIface
 * and base UfoTask.
 * 2) assign initial values for the class attributes.
 */

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rgba_backproject_task_setup;
    iface->get_num_inputs = ufo_rgba_backproject_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rgba_backproject_task_get_num_dimensions;
    iface->get_mode = ufo_rgba_backproject_task_get_mode;
    iface->get_requisition = ufo_rgba_backproject_task_get_requisition;
    iface->process = ufo_rgba_backproject_task_process;
    iface->generate = ufo_rgba_backproject_task_generate;
}

static void
ufo_rgba_backproject_task_class_init (UfoRGBABackprojectTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    oclass->set_property = ufo_rgba_backproject_task_set_property;
    oclass->get_property = ufo_rgba_backproject_task_get_property;
    oclass->finalize = ufo_rgba_backproject_task_finalize;
    GParamSpec *double_region_vals = g_param_spec_double ("double-region-values",
                                                          "Double Region values",
                                                          "Elements in double regions",
                                                          -INFINITY,
                                                          INFINITY,
                                                          0.0,
                                                          G_PARAM_READWRITE);
    
    
    /*
    Number of projections processed per one kernel invocation. Defaults to 24 because benchmarks
    have shown that for a given (height x width) of projections kernel-execution-time vs burst
    minimizes at approximately 24 before hitting a plateau.
    */
    properties[PROP_BURST] =
        g_param_spec_uint ("burst",
            "Number of projections processed per one kernel invocation",
            "Number of projections processed per one kernel invocation",
            // Benchmarking showed that with 24 projections being processed together we land with
            // the most optimal runtime efficiency. The runtime for each back-projection kernel
            // invocation is bottle-necked by the loop over the number of projections. Beyond 24
            // projections we tend to hit the plateau in reduction of total back-projection runtime.
            1, 128, 24,
            G_PARAM_READWRITE);
    
    // Total number of projections to be processed.
    properties[PROP_NUM_PROJECTIONS] =
        g_param_spec_uint ("num-projections",
            "Number of projections",
            "Number of projections",
            0, 32768, 0,
            G_PARAM_READWRITE);

    // Overall angle of rotation during data acquisition, typically pi or 2 * pi.
    properties[PROP_OVERALL_ANGLE] =
        g_param_spec_double ("overall-angle",
            "Angle covered by all projections [rad]",
            "Angle covered by all projections [rad] (can be negative for negative steps "
            "in case only num-projections is specified",
            -G_MAXDOUBLE, G_MAXDOUBLE, G_PI,
            G_PARAM_READWRITE);

    properties[PROP_X_START] =
        g_param_spec_uint ("x-start",
            "First reconstructed coordinate along both in-slice axes",
            "Inclusive first reconstructed coordinate along both in-slice axes",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_X_END] =
        g_param_spec_uint ("x-end",
            "End of the reconstructed interval along both in-slice axes",
            "Exclusive end of the reconstructed interval; zero uses the projection width",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    
    /*
    Geometric axis of rotation for tomographic reconstruction. This is a floating point value,
    which represents a precise pixel position in the width-dimension of the projection. It is
    floating point number because we regard for interpolation on GPUs.
    */
    properties[PROP_CENTER_POSITION_X] =
        g_param_spec_value_array ("center-position-x",
            "Global x center (horizontal in a projection) of the volume with respect to projections",
            "Global x center (horizontal in a projection) of the volume with respect to projections",
            double_region_vals,
            G_PARAM_READWRITE);

    /*
    Reference middle position in the height-dimension. This property along with the region determines
    which horizontal slices from the projection would be reconstructed. In its default form we set
    this property to the middle of height of the projection.
    */
    properties[PROP_CENTER_POSITION_Z] =
        g_param_spec_value_array ("center-position-z",
            "Global z center (vertical in a projection) of the volume with respect to projections",
            "Global z center (vertical in a projection) of the volume with respect to projections",
            double_region_vals,
            G_PARAM_READWRITE);

    /*
    Region property along with center-position-z provides fine-granular control over which slices
    are to be reconstructed. As an example if projection height is 2016 and we want to reconstruct
    all the slices from the projection then we'd set center-position-z to 1008 and region would be
    set to [-1008,1008,1].
    */
    properties[PROP_REGION] =
        g_param_spec_value_array ("region",
            "Region for the parameter along z-axis as (from, to, step)",
            "Region for the parameter along z-axis as (from, to, step)",
            double_region_vals,
            G_PARAM_READWRITE);

    /*
    Addressing mode plays a role in the interpolation, especially when we are nearing an edge of the
    projection. It determines how we'd sample for missing data. Addressing mode none means missing
    data would be assumed as 0 during interpolation.
    */
    properties[PROP_ADDRESSING_MODE] =
        g_param_spec_enum ("addressing-mode",
            "Outlier treatment (\"none\", \"clamp\", \"clamp_to_edge\", \"repeat\")",
            "Outlier treatment (\"none\", \"clamp\", \"clamp_to_edge\", \"repeat\")",
            g_enum_register_static ("ufo_gbp_addressing_mode", addressing_values),
            CL_ADDRESS_CLAMP,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);
    g_type_class_add_private (oclass, sizeof(UfoRGBABackprojectTaskPrivate));
}

static void
ufo_rgba_backproject_task_init(UfoRGBABackprojectTask *self)
{
    self->priv = UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(self);
    self->priv->resources = NULL;
    /// OpenCL resources
    self->priv->context = NULL;
    self->priv->accumulate_kernel = NULL;
    self->priv->backproject_kernel = NULL;
    self->priv->distribute_kernel = NULL;
    /// Properties
    self->priv->overall_angle = G_PI;
    self->priv->burst = 24;
    self->priv->num_projections = 0;
    self->priv->x_start = 0;
    self->priv->x_end = 0;
    self->priv->center_position_x = ufo_scarray_new(3, G_TYPE_DOUBLE, NULL);
    self->priv->center_position_z = ufo_scarray_new(3, G_TYPE_DOUBLE, NULL);
    self->priv->region = ufo_scarray_new(3, G_TYPE_DOUBLE, NULL);
    self->priv->addressing_mode = CL_ADDRESS_CLAMP;
    self->priv->sampler = NULL;
    self->priv->num_slices_actual = 0;
    self->priv->num_slices_processing = 0;
    self->priv->generated = 0;
    self->priv->region_params_checked = FALSE;
    self->priv->distributed = FALSE;
    self->priv->projection_width = 0;
    self->priv->projection_height = 0;
    self->priv->reconstruction_side = 0;
    /// Internal buffers
    self->priv->device_buffer_projections = NULL;
    self->priv->device_texture_projections = NULL;
    self->priv->host_buffer_angles = NULL;
    self->priv->device_buffer_angles = NULL;
    self->priv->device_coalesced_slices = NULL;
    self->priv->device_final_slices = NULL;
}
