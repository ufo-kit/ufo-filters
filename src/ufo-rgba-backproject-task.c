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
#include "common/ufo-scarray.h"
#include "ufo-rgba-backproject-task.h"

struct _UfoRGBABackprojectTaskPrivate {
    // Settings
    guint burst;
    guint num_projections;
    UfoScarray *center_position_x;
    UfoScarray *center_position_z;
    UfoScarray *region;
    // OpenCL
    cl_context context;
    cl_kernel accumulate_kernel;
    cl_kernel backproject_kernel;
    cl_kernel distribute_kernel;
    // Internal
    UfoResources *resources;
    gsize num_slices_actual;
    gsize num_slices_processing;
    gsize generated;
    gdouble overall_angle;
    gboolean can_alloc_dev_mem;
    // Buffers
    float *host_buffer_cosine;
    float *host_buffer_sine;
    cl_mem device_buffer_projections;
    cl_mem device_texture_projections;
    cl_mem device_buffer_cosine;
    cl_mem device_buffer_sine;
    cl_mem device_coalesced_slices;
    cl_mem device_final_slices;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRGBABackprojectTask, ufo_rgba_backproject_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_RGBA_BACKPROJECT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
UFO_TYPE_RGBA_BACKPROJECT_TASK, UfoRGBABackprojectTaskPrivate))

enum {
    PROP_0,
    PROP_BURST,
    PROP_NUM_PROJECTIONS,
    PROP_CENTER_POSITION_X,
    PROP_CENTER_POSITION_Z,
    PROP_REGION,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

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
    // Allocate host-side buffers for cosine and sine components.
    if (!priv->num_projections) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "number of projections not set");
        return;
    }
    priv->host_buffer_cosine = (float*) calloc(priv->num_projections, sizeof(float));
    priv->host_buffer_sine = (float*) calloc(priv->num_projections, sizeof(float));
    const float ang_delta = CL_M_PI_F / (float) priv->num_projections;
    for (uint32_t theta = 0; theta < priv->num_projections; theta++) {
        priv->host_buffer_cosine[theta] = (float) cosf(theta * ang_delta);
        priv->host_buffer_sine[theta] = (float) sinf(theta * ang_delta);
    }
    // Allocate device-side buffers for cosine and sine components.
    if (!priv->burst) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "burst not set");
        return;
    }
    cl_int cl_error;
    if (!priv->device_buffer_cosine) {
        priv->device_buffer_cosine = clCreateBuffer(priv->context, CL_MEM_READ_ONLY,
            priv->burst * sizeof(float), NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }
    if (!priv->device_buffer_sine) {
        priv->device_buffer_sine = clCreateBuffer(priv->context, CL_MEM_READ_ONLY,
            priv->burst * sizeof(float), NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }
    // Determine number of slices to back project and produce depending on region parameter.
    gdouble region_start, region_stop, region_step;
    if (UFO_MATH_ARE_ALMOST_EQUAL (ufo_scarray_get_double (priv->region, 2), 0)) {
        region_start = 0.0f;
        region_stop = 1.0f;
        region_step = 1.0f;
    } else {
        region_start = ufo_scarray_get_double(priv->region, 0);
        region_stop = ufo_scarray_get_double(priv->region, 1);
        region_step = ufo_scarray_get_double(priv->region, 2);
    }
    g_log ("rbp", G_LOG_LEVEL_DEBUG, "region: [start=%g, stop=%g, step=%g]",
        region_start, region_stop, region_step);
    priv->num_slices_actual = (gsize) ceil((region_stop - region_start) / region_step);
    priv->num_slices_processing = (gsize)(ceil((gdouble) priv->num_slices_actual / (gdouble) 4) * 4);
    g_log ("rbp", G_LOG_LEVEL_DEBUG, "backprojecting %lu", priv->num_slices_processing);
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
    // Set output size requisition.
    requisition->n_dims = 2;
    requisition->dims[0] = in_req.dims[0];
    requisition->dims[1] = in_req.dims[0];
    // Check feasibility for memory allocation.
    if (!priv->can_alloc_dev_mem) {
        // Check feasibility of device memory allocation.
        gsize projections_size = priv->burst * in_req.dims[0] * priv->num_slices_processing * sizeof (cl_float);
        gsize slice_size = requisition->dims[0] * requisition->dims[1] * sizeof(cl_float);
        gsize volume_size = slice_size * priv->num_slices_processing;
        GValue *max_mem_alloc_size_gvalue = ufo_gpu_node_get_info (node, UFO_GPU_NODE_INFO_MAX_MEM_ALLOC_SIZE);
        // Even if a card claims to be able to allocate more than 4 GB (e.g. RTX* 8000) we get OpenCL
        // errors, so limit it to 4 GB
        cl_ulong max_mem_alloc_size = MIN (g_value_get_ulong (max_mem_alloc_size_gvalue), ((cl_ulong) 1) << 32);
        g_value_unset (max_mem_alloc_size_gvalue);
        if (projections_size + volume_size > max_mem_alloc_size) {
            g_set_error_literal (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                                    "volume size doesn't fit to memory");
            return;
        }
        priv->can_alloc_dev_mem = TRUE;
    }
    // Allocate device side ring-buffer and additional resources using requisitions.
    cl_int cl_error;
    cl_command_queue cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    if (!priv->device_buffer_projections) {
        priv->device_buffer_projections = clCreateBuffer(
            priv->context, CL_MEM_READ_ONLY,
            priv->burst * in_req.dims[0] * priv->num_slices_processing * sizeof(float),
            NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
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
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }
    if (!priv->device_coalesced_slices) {
        size_t coal_slice_size = requisition->dims[0] * requisition->dims[0] * (
            priv->num_slices_processing / 4) * sizeof(cl_float4);
        priv->device_coalesced_slices = clCreateBuffer(priv->context, CL_MEM_WRITE_ONLY,
            coal_slice_size, NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
        // Zero-fill the coalesced slices buffer. Backprojected values are added to it during each
        // kernel execution. 
        float fill = 0.0f;
        UFO_RESOURCES_CHECK_CLERR (
            clEnqueueFillBuffer (cmd_queue, priv->device_coalesced_slices, &fill, sizeof(cl_float),
            0, coal_slice_size, 0, NULL, NULL));
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
 * In this simpler scenatio `actual_burst` would be the configured `priv->burst` because we have
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
    // Copy the current projection to its correct position of the device ring buffer. This requires
    // that we take note of the following.
    // - Offset: We need to correctly calculate the position of the device side ring buffer where
    // to update the current projection. While `in_req.dims[0] * priv->num_slices_processing * sizeof(float)`
    // gives the size of each projection in bytes multiplying that by idx_actual_burst yields the
    // required offset from the start of the buffer because idx_actual_burst is the index of the
    // projection inside its burst.
    // - Size: in_req.dims[0] * priv->num_slices_processing * sizeof(float) is the size of each projection in
    // bytes.
    // Using offset and size we can update the device side ring buffer for each incoming projection.
    float *curr_proj_array =  ufo_buffer_get_host_array(inputs[0], cmd_queue);
    /// TODO: Projection offset depends upon center_position_z as well.
    gsize projection_offset = (gsize) ufo_scarray_get_double(priv->region, 0) * in_req.dims[0] * sizeof(float);
    UFO_RESOURCES_CHECK_CLERR (clEnqueueWriteBuffer (cmd_queue, priv->device_buffer_projections,
        CL_TRUE,
        idx_actual_burst * in_req.dims[0] * priv->num_slices_processing * sizeof(float), // Offset
        in_req.dims[0] * priv->num_slices_processing * sizeof(float), // Size
        curr_proj_array + projection_offset, 0, NULL, NULL));
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
        g_log ("rbp", G_LOG_LEVEL_DEBUG, "processing %u projections starting from %u", actual_burst,
            global_proj_idx);
        /// STAGE: ACCUMULATE (Packs four rows of the projection into one using RGBA format)
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 0, sizeof(cl_mem),
        &priv->device_buffer_projections));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 1, sizeof(cl_mem),
        &priv->device_texture_projections));
        const size_t accumulate_work_size[] = {in_req.dims[0], priv->num_slices_processing / 4, actual_burst};
        ufo_profiler_call_blocking (profiler, cmd_queue, priv->accumulate_kernel, 3,
            accumulate_work_size, NULL);
        /// STAGE: BACKPROJECT
        UFO_RESOURCES_CHECK_CLERR (clEnqueueWriteBuffer (
            cmd_queue, priv->device_buffer_cosine, CL_TRUE, 0,
            actual_burst * sizeof(float), priv->host_buffer_cosine + global_proj_idx,
            0, NULL, NULL));
        UFO_RESOURCES_CHECK_CLERR (clEnqueueWriteBuffer (
            cmd_queue, priv->device_buffer_sine, CL_TRUE, 0,
            actual_burst * sizeof(float), priv->host_buffer_sine + global_proj_idx,
            0, NULL, NULL));
        /// TODO: The dimensionality of the work size would change when we incorporate the region
        // property. For the time being we assume that we are reconstructing all slices.
        const size_t bp_work_size[] = {in_req.dims[0], in_req.dims[0], priv->num_slices_processing / 4};
        const cl_float center_position_x = (cl_float) ufo_scarray_get_double(priv->center_position_x, 0);
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 0, sizeof(cl_mem),
        &priv->device_texture_projections));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 1, sizeof(cl_mem),
        &priv->device_coalesced_slices));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 2, sizeof(cl_mem),
        &priv->device_buffer_cosine));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 3, sizeof(cl_mem),
        &priv->device_buffer_sine));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 4, sizeof(cl_float),
        &center_position_x));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 5, sizeof(cl_uint),
        &actual_burst));
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
    cl_int cl_error;
    if (!priv->device_final_slices) {
        priv->device_final_slices = clCreateBuffer(priv->context, CL_MEM_WRITE_ONLY,
            requisition->dims[0] * requisition->dims[1] * priv->num_slices_processing * sizeof(cl_float),
            NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }
    /// STAGE: DISTRIBUTE (Spread the values packed into float4 buffer into separate slices)
    const size_t dist_work_size[] = {
        requisition->dims[0], requisition->dims[1], priv->num_slices_processing / 4};
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->distribute_kernel, 0, sizeof(cl_mem),
    &priv->device_coalesced_slices));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->distribute_kernel, 1, sizeof(cl_mem),
    &priv->device_final_slices));
    ufo_profiler_call_blocking (profiler, cmd_queue, priv->distribute_kernel, 3, dist_work_size,
        NULL);
    /// STAGE: OUTPUT
    guint processed_proj_count;
    g_object_get (task, "num_processed", &processed_proj_count, NULL);
    if (processed_proj_count < priv->num_projections) {
        // ERROR_CONDITION: Since generate is called at the end of processing all inputs here we
        // expect that all of the priv->num_projections number of projections are processed. If
        // that's not the case backprojection workflow encountered an anomaly and we should not
        // produce any slices.
        g_warning ("rgba-backproject received only %u projections out of %u "
                   "specified, no outuput will be generated", processed_proj_count,
                   priv->num_projections);
        return FALSE;
    }
    if (priv->generated >= priv->num_slices_actual) {
        // EXIT_CONDITION: No need to process further if we have already generated the required
        // number of slices.  
        return FALSE;
    }
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
    size_t src_origin[3] = {0, 0, priv->generated % priv->num_slices_actual};
    size_t dst_origin[3] = {0, 0, 0};
    size_t region[3] = {row_pitch, requisition->dims[1], 1};
    g_log ("rbp", G_LOG_LEVEL_DEBUG, "generating slice %lu", priv->generated + 1);
    g_log ("rbp", G_LOG_LEVEL_DEBUG, "src_origin: %lu %lu %lu", src_origin[0], src_origin[1],
        src_origin[2]);
    g_log ("rbp", G_LOG_LEVEL_DEBUG, "region: %lu %lu %lu", region[0], region[1], region[2]);
    g_log ("rbp", G_LOG_LEVEL_DEBUG, "row pitch %lu, slice pitch %lu", row_pitch, slice_pitch);
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
        case PROP_CENTER_POSITION_X:
            ufo_scarray_get_value (priv->center_position_x, value);
            break;
        case PROP_CENTER_POSITION_Z:
            ufo_scarray_get_value (priv->center_position_z, value);
            break;
        case PROP_REGION:
            ufo_scarray_get_value(priv->region, value);
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
        case PROP_CENTER_POSITION_X:
            ufo_scarray_set_value (priv->center_position_x, value);
            break;
        case PROP_CENTER_POSITION_Z:
            ufo_scarray_set_value (priv->center_position_z, value);
            break;
        case PROP_REGION:
            ufo_scarray_set_value(priv->region, value);
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
    if (priv->device_buffer_cosine) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_buffer_cosine));
        priv->device_buffer_cosine = NULL;
    }
    if (priv->device_buffer_sine) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_buffer_sine));
        priv->device_buffer_sine = NULL;
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
    if (priv->host_buffer_cosine) {
        free(priv->host_buffer_cosine);
        priv->host_buffer_cosine = NULL;
    }
    if (priv->host_buffer_sine) {
        free(priv->host_buffer_sine);
        priv->host_buffer_sine = NULL;
    }
    G_OBJECT_CLASS(ufo_rgba_backproject_task_parent_class)->finalize(object);
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
    properties[PROP_BURST] =
        g_param_spec_uint ("burst",
            "Number of projections processed per one kernel invocation",
            "Number of projections processed per one kernel invocation",
            0, 128, 24,
            G_PARAM_READWRITE);
    
    properties[PROP_NUM_PROJECTIONS] =
        g_param_spec_uint ("num-projections",
            "Number of projections",
            "Number of projections",
            0, 32768, 0,
            G_PARAM_READWRITE);

    properties[PROP_CENTER_POSITION_X] =
        g_param_spec_value_array ("center-position-x",
            "Global x center (horizontal in a projection) of the volume with respect to projections",
            "Global x center (horizontal in a projection) of the volume with respect to projections",
            double_region_vals,
            G_PARAM_READWRITE);

    properties[PROP_CENTER_POSITION_Z] =
        g_param_spec_value_array ("center-position-z",
            "Global z center (vertical in a projection) of the volume with respect to projections",
            "Global z center (vertical in a projection) of the volume with respect to projections",
            double_region_vals,
            G_PARAM_READWRITE);

    properties[PROP_REGION] =
        g_param_spec_value_array ("region",
            "Region for the parameter along z-axis as (from, to, step)",
            "Region for the parameter along z-axis as (from, to, step)",
            double_region_vals,
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
    self->priv->burst = 0;
    self->priv->num_projections = 0;
    self->priv->center_position_x = ufo_scarray_new(3, G_TYPE_DOUBLE, NULL);
    self->priv->center_position_z = ufo_scarray_new(3, G_TYPE_DOUBLE, NULL);
    self->priv->region = ufo_scarray_new(3, G_TYPE_INT, NULL);
    self->priv->num_slices_actual = 0;
    self->priv->num_slices_processing = 0;
    self->priv->generated = 0;
    self->priv->can_alloc_dev_mem = FALSE;
    /// Internal buffers
    self->priv->device_buffer_projections = NULL;
    self->priv->device_texture_projections = NULL;
    self->priv->host_buffer_cosine = NULL;
    self->priv->device_buffer_cosine = NULL;
    self->priv->host_buffer_sine = NULL;
    self->priv->device_buffer_sine = NULL;
    self->priv->device_coalesced_slices = NULL;
    self->priv->device_final_slices = NULL;
}