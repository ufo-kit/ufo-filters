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
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-online-backproject-task.h"


struct _UfoOnlineBackprojectTaskPrivate {
    guint burst;
    guint num_projections;
    UfoScarray *region;
    UfoScarray *center_position_x;
    cl_context context;
    cl_kernel accumulate_kernel;
    cl_kernel backproject_kernel;
    cl_kernel distribute_kernel;
    float *projections;
    cl_mem volume;
    // guint num_slices;
    // guint num_slices_per_chunk;
    // guint num_chunks;
    // guint generated;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoOnlineBackprojectTask, ufo_online_backproject_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ONLINE_BACKPROJECT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), \
UFO_TYPE_ONLINE_BACKPROJECT_TASK, UfoOnlineBackprojectTaskPrivate))

enum {
    PROP_0,
    PROP_BURST,
    PROP_REGION,
    PROP_CENTER_POSITION_X,
    PROP_NUM_PROJECTIONS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_online_backproject_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ONLINE_BACKPROJECT_TASK, NULL));
}


/**
 * ufo_online_backproject_task_get_num_inputs:
 * @task: A #UfoTask.
 *
 * Specifies the number of inputs for the online back-project task. Since we want to process each
 * single incoming projection, we specify that the task expects one input.
 *
 * Returns: Number of incoming projections at a time.
 */
static guint
ufo_online_backproject_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

/**
 * ufo_online_backproject_task_get_num_dimensions:
 * @task: A #UfoTask.
 * @input: Dimension of the input. 
 * 
 * Specifies the number of dimensions of the input. A single incoming projection has 2 dimensions.
 * 
 * Returns: Number of dimensions for single incoming projection.
 */
static guint
ufo_online_backproject_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

/**
 * ufo_online_backproject_task_get_mode:
 * @task: A #UfoTask.
 * 
 * Specifies the mode in which the task operates. The online back-project task is designed to
 * process a stream of incoming projections, accumulating them in a buffer and then
 * back-projecting them onto a 3D volume. It operates in a reductor mode, meaning it
 * expects to receive a stream of inputs and produce an output and the task is designed to run on
 * GPU devices.
 * 
 * Returns: A bitwise OR of the task modes that this task supports.
 */
static UfoTaskMode
ufo_online_backproject_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_GPU;
}

/**
 * ufo_online_backproject_task_setup:
 * @task: A #UfoTask.
 * @resources: A #UfoResources instance containing the OpenCL context and kernels.
 * @error: A pointer to a #GError that will be set if an error occurs.
 * 
 * This function sets up the OpenCL and other runtime resources required for the online
 * back-project task and called once per task instance. We initialize all reusable resources here.
 */
static void
ufo_online_backproject_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoOnlineBackprojectTaskPrivate *priv = UFO_ONLINE_BACKPROJECT_TASK_GET_PRIVATE(task);
    /// Instantiate resources
    if (!priv->num_projections) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "Number of projections not set");
        return;
    }
    priv->host_buffer_cosine = g_malloc0(priv->num_projections, sizeof(float));
    priv->host_buffer_sine = g_malloc0(priv->num_projections, sizeof(float));
    const float ang_delta = CL_M_PI_F / (float) priv->num_projections;
    for (uint32_t theta = 0; theta < priv->num_projections; theta++) {
        priv->host_buffer_cosine[theta] = (float) cosf(theta * ang_delta);
        priv->host_buffer_sine[theta] = (float) sinf(theta * ang_delta);
    }
    /// Instantiate OpenCL resources
    cl_int cl_error;
    priv->context = ufo_resources_get_context(resources);
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    priv->cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));
    priv->accumulate_kernel = ufo_resources_get_kernel(resources, "online-backproject.cl",
        "accumulate", NULL, cl_error);
    UFO_RESOURCES_CHECK_CLERR (cl_error);
    priv->backproject_kernel = ufo_resources_get_kernel(resources, "online-backproject.cl",
        "backproject", NULL, cl_error);
    UFO_RESOURCES_CHECK_CLERR (cl_error);
    priv->distribute_kernel = ufo_resources_get_kernel(resources, "online-backproject.cl",
        "distribute", NULL, cl_error);
    UFO_RESOURCES_CHECK_CLERR (cl_error);
    UFO_RESOURCES_CHECK_CLERR (clRetainKernel(priv->accumulate_kernel));
    UFO_RESOURCES_CHECK_CLERR (clRetainKernel(priv->backproject_kernel));
    UFO_RESOURCES_CHECK_CLERR (clRetainKernel(priv->distribute_kernel));
}

/**
 * ufo_online_backproject_task_requisition:
 * @task: A #UfoTask.
 * @inputs: Input #UfoBuffer resources. Number of buffers depends on `get_num_inputs` function.
 * @requisition: A #UfoRequisition to describe the dimensions of the output data.
 * @error: A pointer to a #GError that will be set if an error occurs.
 * 
 * Called for each iteration of the task right before process to calrify, which size of an
 * output buffer would be required depending on the inputs.
 */
static void
ufo_online_backproject_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition,
                                 GError **error)
{
    UfoOnlineBackprojectTaskPrivate *priv = UFO_ONLINE_BACKPROJECT_TASK_GET_PRIVATE(task);
    UfoRequisition in_req;
    gdouble region_start, region_stop, region_step;
    ufo_buffer_get_requisition(inputs[0], &in_req);
    /// NOTE: The argument requisition spcifies the dimensions of the output data that we will
    // produce in the generate function. For our task we specify two dimensions because we want
    // to produce 2D slices from generate.
    /// TODO: Region is disabled until we achieve a correct version of the task that processes all
    // slices, which is a simpler case to handle. Therefore all slices buffers are allocated with
    // full sizes for the time being and priv->num_slices is set to the full height of the
    // projection. When we implement region requisition, num_slices and buffer sizes would be adapted
    // accordingly.
    // if (ufo_scarray_get_double(priv->region, 2) == 0.0) {
    //     region_start = 0.0f;
    //     region_stop = 1.0f;
    //     region_step = 1.0f;
    // } else {
    //     region_start = ufo_scarray_get_double(priv->region, 0);
    //     region_stop = ufo_scarray_get_double(priv->region, 1);
    //     region_step = ufo_scarray_get_double(priv->region, 2);
    // }
    // priv->num_slices = (guint) ceil((region_stop - region_start) / region_step);
    requisition->n_dims = 2;
    requisition->dims[0] = in_req.dims[0];
    requisition->dims[1] = in_req.dims[0];
    priv->num_slices = in_req.dims[1];
    // Allocate host ring buffer and additional buffer memories here because this is the first place
    // where we can know the projection shape.
    if (!priv->burst) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "Burst not set");
        return;
    }
    if (!priv->host_buffer_projections) {
        priv->host_buffer_projections = g_malloc0(
            priv->burst * in_req.dims[0] * in_req.dims[1] * sizeof(float));
    }
    cl_int cl_error;
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cl_command_queue cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    if (!priv->device_buffer_projections) {
        priv->device_buffer_projections = clCreateBuffer(
            priv->context, CL_MEM_READ_ONLY,
            priv->burst * in_req.dims[0] * in_req.dims[1] * sizeof(float),
            NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }
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
    if (!priv->device_texture_projections) {
        cl_image_format fmt = {CL_RGBA, CL_HALF_FLOAT};
        cl_image_desc desc = {0};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D_ARRAY;
        desc.image_width = in_req.dims[0];
        desc.image_height = in_req.dims[1] / 4;
        desc.image_depth = 0;
        desc.image_array_size = priv->burst;
        priv->device_texture_projections = clCreateImage(priv->context, CL_MEM_READ_WRITE,
            &fmt, &desc, NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }
    if (!priv->device_coalesced_slices) {
        size_t coal_slice_size = requisition->dims[0] * requisition->dims[0] * (
            priv->num_slices / 4) * sizeof(cl_float4)
        priv->device_coalesced_slices = clCreateBuffer(priv->context, CL_MEM_WRITE_ONLY,
            coal_slice_size, NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
        /// NOTE: We zero-fill the coalesced slices buffer because we add the backprojected values
        // onto it in each burst.
        float fill = 0.0f;
        UFO_RESOURCES_CHECK_CLERR (
            clEnqueueFillBuffer (cmd_queue, priv->device_coalesced_slices, &fill, sizeof(cl_float),
            0, coal_slice_size, 0, NULL, NULL));
    }
}

/**
 * ufo_online_backproject_task_process:
 * @task: A #UfoTask.
 * @inputs: Input #UfoBuffer resources. Number of buffers depends on `get_num_inputs` function.
 * @output: A #UfoBuffer having the output if any. Output buffer may not be used at this stage.
 * @requisition: A #UfoRequisition to describe the dimensions of the output data.
 * 
 * Called for each iteration of the task to process the input data.
 */
static gboolean
ufo_online_backproject_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoOnlineBackprojectTaskPrivate *priv = UFO_ONLINE_BACKPROJECT_TASK_GET_PRIVATE(task);
    UfoRequisition in_req;
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cl_command_queue cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    ufo_buffer_get_requisition (inputs[0], &in_req);
    /// NOTE: Determine whether we are at a complete burst or incomplete burst situation. Complete
    // burst means we are at a point when we can still process (priv->burst) number of subsequent
    // projections. In contrast incomplete burst means that we are approaching the end of processing
    // priv->num_projections and less then (priv->burst) number projections are left to process.
    // processed_proj_count => Number of processed inputs/projections (tracked by ufo task node).
    // actual_burst => Actual number of projection that we can process with next kernel execution.
    // idx_actual_burst => Index of current projection in actual burst. In both scenarios we use
    // modulo with actual burst to prevent going out of bounds.
    // Both the actual burst size and projection index in actual burst is needed to correctly update
    // and fetch projections from host side ring buffer.
    guint idx_actual_burst, processed_proj_count, actual_burst;
    g_object_get (task, "num_processed", &processed_proj_count, NULL);
    if (processed_proj_count >= (priv->num_projections / priv->burst) * priv->burst) {
        /// Scenario: Incomplete burst
        // Expression ((priv->num_projections / priv->burst) * priv->burst) tells us total number of
        // projections we can process with the burst we have configured. If our currently processed
        // projection count exceeds that number it means we have less then (priv->burst) left to
        // process out of all priv->num_projections. In that case actual burst would be remainder
        // number of projections after processing all the complete bursts. We calculate projection
        // index in actual burst by calculating the difference between current processed projection
        // count and total number of projection that can be processed with complete burst.
        actual_burst = priv->num_projections % priv->burst;
        idx_actual_burst = (processed_proj_count - (
            priv->num_projections / priv->burst) * priv->burst) % actual_burst;
    } else {
        /// Scenario: Complete burst
        // In this simpler scenatio actual burst would be the configured burst since we have
        // sufficient number of projections still left to process. We calculate the projection index
        // in actual burst using the straight-forward modulo operation.
        actual_burst = priv->burst;
        idx_actual_burst = processed_proj_count % actual_burst;
    }
    // Copy the current projection array to the correct position of the host ring buffer.
    float *curr_proj_array =  ufo_buffer_get_host_array(inputs[0], cmd_queue);
    memcpy(
        curr_proj_array, 
        priv->host_buffer_projections + (idx_actual_burst * in_req.dims[0] * in_req.dims[1]),
        in_req.dims[0] * in_req.dims[1] * sizeof(float));
    /// NOTE: Start processing projections once we have accumulated actual_burst number of
    // projections in the host side ring buffer.
    if (idx_actual_burst + 1 == actual_burst) {
        /// NOTE: We compute the global index of the first projection to be processed in the actual
        // burst. This index is especially useful for us to select the angular cosine and sine
        // parameters from the host side buffer to device side since we also know the size of the
        // actual burst.
        cl_uint global_proj_idx = (cl_uint) (processed_proj_count + 1 - actual_burst);
        UfoProfiler *profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
        /// Stage: Accumulate
        UFO_RESOURCES_CHECK_CLERR (clEnqueueWriteBuffer (
            cmd_queue, priv->device_buffer_projections, CL_TRUE, 0,
            actual_burst * in_req.dims[0] * in_req.dims[1] * sizeof(float),
            priv->host_buffer_projections + (actual_burst * in_req.dims[0] * in_req.dims[1]),
            0, NULL, NULL));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 0, sizeof(cl_mem),
        &priv->device_buffer_projections));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 1, sizeof(cl_mem),
        &priv->device_texture_projections));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 2, sizeof(cl_uint),
        &actual_burst));
        const size_t accumulate_work_size[] = {in_req.dims[0], in_req.dims[1] / 4, actual_burst};
        ufo_profiler_call_blocking (profiler, cmd_queue, priv->accumulate_kernel, 3,
            accumulate_work_size, NULL);
        /// Stage: Backproject
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
        const size_t bp_work_size[] = {in_req.dims[0], in_req.dims[0], in_req.dims[1] / 4};
        if (!priv->center_position_x) {
            g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "Rotation axis not set");
            return;
        }
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 0, sizeof(cl_mem),
        &priv->device_texture_projections));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 1, sizeof(cl_mem),
        &priv->device_coalesced_slices));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 2, sizeof(cl_mem),
        &priv->device_buffer_cosine));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 3, sizeof(cl_mem),
        &priv->device_buffer_sine));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 4, sizeof(cl_float),
        &priv->center_position_x));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->backproject_kernel, 5, sizeof(cl_uint),
        &actual_burst));
        ufo_profiler_call_blocking (profiler, cmd_queue, priv->backproject_kernel, 3,
            bp_work_size, NULL);
    }
    return TRUE;
}

/**
 * ufo_online_backproject_task_generate:
 * @task: A #UfoTask.
 * @output: A #UfoBuffer to contain the output from the task.
 * @requisition: A #UfoRequisition to describe the dimensions of the output data.
 * 
 * Called at the end of all process iterations to generate the output from the task.
 */
static gboolean
ufo_online_backproject_task_generate (UfoTask *task,
                                       UfoBuffer *output,
                                       UfoRequisition *requisition)
{
    UfoOnlineBackprojectTaskPrivate *priv = UFO_ONLINE_BACKPROJECT_TASK_GET_PRIVATE(task);
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cl_command_queue cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    /// NOTE: We deallocate all unnecessary resources before distributing the coalesced slices. This
    // should help reducing the memory footprint before generating final output.
    if (priv->host_buffer_projections) {
        g_free(priv->host_buffer_projections);
        priv->host_buffer_projections = NULL;
    }
    if (priv->host_buffer_cosine) {
        g_free(priv->host_buffer_cosine);
        priv->host_buffer_cosine = NULL;
    }
    if (priv->host_buffer_sine) {
        g_free(priv->host_buffer_sine);
        priv->host_buffer_sine = NULL;
    }
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
    cl_int cl_error;
    if (!priv->device_final_slices) {
        priv->device_final_slices = clCreateBuffer(priv->context, CL_MEM_WRITE_ONLY,
            requisition->dims[0] * requisition->dims[1] * priv->num_slices * sizeof(cl_float),
            NULL, &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }
    /// Stage: Distribute
    const size_t dist_work_size[] = {requisition->dims[0], requisition->dims[1], priv->num_slices / 4};
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->distribute_kernel, 0, sizeof(cl_mem),
    &priv->device_coalesced_slices));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->distribute_kernel, 1, sizeof(cl_mem),
    &priv->device_final_slices));
    ufo_profiler_call_blocking (profiler, cmd_queue, priv->distribute_kernel, 3, dist_work_size, NULL);
    // Clean up device resources for the coalesced slices.
    if (priv->device_coalesced_slices) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_coalesced_slices));
        priv->device_coalesced_slices = NULL;
    }
    /// Stage: Output
    cl_mem out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    guint processed_proj_count;
    g_object_get (task, "num_processed", &processed_proj_count, NULL);
    if (processed_proj_count != priv->num_projections) {
        g_warning ("online-backproject received only %u projections out of %u "
                   "specified, no outuput will be generated", processed_proj_count,
                   priv->num_projections);
        return FALSE;
    }
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
    size_t src_origin[3] = {0, 0, priv->generated % priv->num_slices};
    size_t dst_origin[3] = {0, 0, 0};
    size_t region[3] = {src_row_pitch, requisition->dims[1], 1};
    g_log ("gbp", G_LOG_LEVEL_DEBUG, "Generating slice %u", priv->generated + 1);
    g_log ("gbp", G_LOG_LEVEL_DEBUG, "src_origin: %lu %lu %lu", src_origin[0], src_origin[1], src_origin[2]);
    g_log ("gbp", G_LOG_LEVEL_DEBUG, "region: %lu %lu %lu", region[0], region[1], region[2]);
    g_log ("gbp", G_LOG_LEVEL_DEBUG, "row pitch %lu, slice pitch %lu", src_row_pitch, src_slice_pitch);
    UFO_RESOURCES_CHECK_CLERR (clEnqueueCopyBufferRect (cmd_queue,
                                                        priv->device_final_slices, out_mem,
                                                        src_origin, dst_origin, region,
                                                        src_row_pitch, src_slice_pitch,
                                                        src_row_pitch, 0, 0, NULL, NULL));
    priv->generated++;
    return TRUE;
}

static void
ufo_online_backproject_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoOnlineBackprojectTaskPrivate *priv = UFO_ONLINE_BACKPROJECT_TASK_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_BURST:
            priv->burst = g_value_get_uint(value);
            break;
        case PROP_REGION:
            ufo_scarray_get_value(priv->region, value);
            break;
        case PROP_CENTER_POSITION_X:
            priv->center_position_x = g_value_get_double (value);
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
ufo_online_backproject_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoOnlineBackprojectTaskPrivate *priv = UFO_ONLINE_BACKPROJECT_TASK_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_BURST:
            g_value_set_uint(value, priv->burst);
            break;
        case PROP_REGION:
            ufo_scarray_set_value(priv->region, value);
            break;
        case PROP_CENTER_POSITION_X:
            g_value_set_double(priv->center_position_x, value);
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
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_online_backproject_task_setup;
    iface->get_num_inputs = ufo_online_backproject_task_get_num_inputs;
    iface->get_num_dimensions = ufo_online_backproject_task_get_num_dimensions;
    iface->get_mode = ufo_online_backproject_task_get_mode;
    iface->get_requisition = ufo_online_backproject_task_get_requisition;
    iface->process = ufo_online_backproject_task_process;
    iface->generate = ufo_online_backproject_task_generate;
}

static void
ufo_online_backproject_task_class_init (UfoOnlineBackprojectTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_online_backproject_task_set_property;
    oclass->get_property = ufo_online_backproject_task_get_property;
    oclass->finalize = ufo_online_backproject_task_finalize;

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

    properties[PROP_REGION] =
        g_param_spec_value_array ("region",
            "Region for the parameter along z-axis as (from, to, step)",
            "Region for the parameter along z-axis as (from, to, step)",
            double_region_vals,
            G_PARAM_READWRITE);

    properties[PROP_CENTER_POSITION_X] =
        g_param_spec_double ("center-position-x",
            "Position of rotation axis",
            "Position of rotation axis",
            -1.0, +32768.0, 0.0,
            G_PARAM_READWRITE);

    properties[PROP_NUM_PROJECTIONS] =
        g_param_spec_uint ("num-projections",
            "Number of projections",
            "Number of projections",
            0, 32768, 0,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoOnlineBackprojectTaskPrivate));
}

static void
ufo_online_backproject_task_init(UfoOnlineBackprojectTask *self)
{
    self->priv = UFO_GENERAL_BACKPROJECT_TASK_GET_PRIVATE(self);
    self->priv->resources = NULL;
    /// OpenCL resources
    self->priv->context = NULL;
    self->priv->cmd_queue = NULL;
    priv->accumulate_kernel = NULL;
    priv->backproject_kernel = NULL;
    priv->distribute_kernel = NULL;
    /// Properties
    self->priv->burst = 0;
    self->priv->overall_angle = G_PI;
    self->priv->num_projections = 0;
    self->priv->center_position_x = 0.0;
    self->priv->region = ufo_scarray_new(3, G_TYPE_DOUBLE, NULL);
    self->priv->num_slices = 0;
    self->priv->generated = 0;
    /// Internal buffers
    self->priv->host_buffer_projections = NULL;
    self->priv->device_buffer_projections = NULL;
    self->priv->device_texture_projections = NULL;
    self->priv->host_buffer_cosine = NULL;
    self->priv->device_buffer_cosine = NULL;
    self->priv->host_buffer_sine = NULL;
    self->priv->device_buffer_sine = NULL;
    self->priv->device_coalesced_slices = NULL;
    self->priv->device_final_slices = NULL;
}

static void
ufo_online_backproject_task_finalize (GObject *object)
{
    UfoOnlineBackprojectTaskPrivate *priv = UFO_ONLINE_BACKPROJECT_TASK_GET_PRIVATE(object);
    if (priv->region) {
        ufo_scarray_free(priv->region);
        priv->region = NULL;
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
    if (priv->device_final_slices) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->device_final_slices));
        priv->device_final_slices = NULL;
    }
    if (priv->cmd_queue) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseCommandQueue (priv->cmd_queue));
        priv->cmd_queue = NULL;
    }
    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }
    if (priv->resources) {
        g_object_unref (priv->resources);
        priv->resources = NULL;
    }
    G_OBJECT_CLASS(ufo_online_backproject_task_parent_class)->finalize(object);
}