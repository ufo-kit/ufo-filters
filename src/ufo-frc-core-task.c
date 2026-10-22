/*
 * Copyright (C) 2011-2017 Karlsruhe Institute of Technology
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

#include <float.h>
#include <math.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-frc-core-task.h"


struct _UfoFrcCoreTaskPrivate {
    cl_context context;
    cl_kernel accumulate_kernel;
    cl_kernel reduce_kernel;
    cl_mem partials;
    gsize partials_size;
    UfoBuffer *first_spectrum;
    UfoRequisition spectrum_req;
    gdouble pixel_size_x;
    gdouble pixel_size_y;
    gdouble ring_width;
    gdouble max_frequency;
    gfloat resolved_ring_width;
    cl_uint num_bins;
    cl_uint num_groups;
    size_t local_size;
    gboolean execution_configured;
    gboolean have_spectrum_req;
    gboolean have_first;
    gboolean result_ready;
    gboolean generated;
    gboolean inputs_stopped;
    gboolean incomplete_warned;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFrcCoreTask, ufo_frc_core_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_FRC_CORE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FRC_CORE_TASK, UfoFrcCoreTaskPrivate))

enum {
    PROP_0,
    PROP_PIXEL_SIZE_X,
    PROP_PIXEL_SIZE_Y,
    PROP_RING_WIDTH,
    PROP_MAX_FREQUENCY,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

static gboolean configure_execution (UfoFrcCoreTaskPrivate *priv,
                                     cl_command_queue queue,
                                     cl_ulong num_pixels);

UfoNode *
ufo_frc_core_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FRC_CORE_TASK, NULL));
}

static gboolean
same_requisition (const UfoRequisition *first,
                  const UfoRequisition *second)
{
    if (first->n_dims != second->n_dims)
        return FALSE;

    for (guint dimension = 0; dimension < first->n_dims; dimension++) {
        if (first->dims[dimension] != second->dims[dimension])
            return FALSE;
    }

    return TRUE;
}

static gboolean
checked_mul_size (gsize a,
                  gsize b,
                  gsize *result)
{
    if (a != 0 && b > G_MAXSIZE / a)
        return FALSE;

    *result = a * b;
    return TRUE;
}

static void
reset_pair (UfoFrcCoreTaskPrivate *priv)
{
    priv->have_first = FALSE;
    priv->result_ready = FALSE;
    priv->generated = FALSE;
}

static void
ufo_frc_core_task_setup (UfoTask *task,
                         UfoResources *resources,
                         GError **error)
{
    UfoFrcCoreTaskPrivate *priv = UFO_FRC_CORE_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainContext (priv->context), error);

    priv->accumulate_kernel = ufo_resources_get_kernel (
        resources, "frc-core.cl", "frc_accumulate_partials", NULL, error);
    if (priv->accumulate_kernel == NULL)
        return;
    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->accumulate_kernel), error);

    priv->reduce_kernel = ufo_resources_get_kernel (
        resources, "frc-core.cl", "frc_reduce_partials", NULL, error);
    if (priv->reduce_kernel == NULL)
        return;
    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->reduce_kernel), error);

    reset_pair (priv);
    priv->inputs_stopped = FALSE;
    priv->incomplete_warned = FALSE;
}

static void
ufo_frc_core_task_get_requisition (UfoTask *task,
                                   UfoBuffer **inputs,
                                   UfoRequisition *requisition,
                                   GError **error)
{
    UfoFrcCoreTaskPrivate *priv = UFO_FRC_CORE_TASK_GET_PRIVATE (task);
    UfoRequisition in_req;
    gdouble delta_kx;
    gdouble delta_ky;
    gdouble resolved_max_frequency;
    gdouble ratio;
    gdouble tolerance;
    gsize bins;
    gsize nx;
    UfoGpuNode *node;
    cl_command_queue queue;
    cl_ulong num_pixels;

    ufo_buffer_get_requisition (inputs[0], &in_req);

    if (in_req.n_dims != 2) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "frc-core requires a two-dimensional spectrum");
        return;
    }

    if (ufo_buffer_get_layout (inputs[0]) != UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "frc-core requires complex-interleaved input");
        return;
    }

    if (in_req.dims[0] < 2 || in_req.dims[0] % 2 != 0) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "frc-core received an invalid complex-interleaved width");
        return;
    }

    if (in_req.dims[1] == 0 || in_req.dims[0] / 2 > G_MAXINT ||
        in_req.dims[1] > G_MAXINT) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "frc-core received unsupported image dimensions");
        return;
    }

    if (!isfinite (priv->pixel_size_x) || priv->pixel_size_x <= 0.0 ||
        !isfinite (priv->pixel_size_y) || priv->pixel_size_y <= 0.0) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "pixel-size-x and pixel-size-y must be finite and positive");
        return;
    }

    if (!isfinite (priv->ring_width) || priv->ring_width < 0.0 ||
        !isfinite (priv->max_frequency) || priv->max_frequency < 0.0) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "ring-width and max-frequency must be finite and non-negative");
        return;
    }

    /* The scheduler asks for a requisition before every process call. Keep
     * the geometry of the first spectrum instead of silently accepting a
     * different shape later in the stream. The process method diagnoses and
     * discards mismatches without terminating the complete graph. */
    if (priv->have_spectrum_req &&
        !same_requisition (&in_req, &priv->spectrum_req)) {
        requisition->n_dims = 2;
        requisition->dims[0] = priv->num_bins;
        requisition->dims[1] = 7;
        return;
    }

    nx = in_req.dims[0] / 2;
    delta_kx = 1.0 / ((gdouble) nx * priv->pixel_size_x);
    delta_ky = 1.0 / ((gdouble) in_req.dims[1] * priv->pixel_size_y);
    priv->resolved_ring_width = (gfloat) (priv->ring_width > 0.0
        ? priv->ring_width
        : MAX (delta_kx, delta_ky));
    resolved_max_frequency = priv->max_frequency > 0.0
        ? priv->max_frequency
        : MIN (1.0 / (2.0 * priv->pixel_size_x),
               1.0 / (2.0 * priv->pixel_size_y));

    if (!isfinite (priv->resolved_ring_width) || priv->resolved_ring_width <= 0.0f ||
        !isfinite (resolved_max_frequency) || resolved_max_frequency <= 0.0) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "resolved ring geometry is invalid");
        return;
    }

    ratio = resolved_max_frequency / (gdouble) priv->resolved_ring_width;
    tolerance = 16.0 * DBL_EPSILON * MAX (1.0, fabs (ratio));
    if (!isfinite (ratio) || ratio + tolerance < 1.0 ||
        floor (ratio + tolerance) > G_MAXUINT) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "ring configuration resolves to an unsupported number of bins");
        return;
    }

    bins = (gsize) floor (ratio + tolerance);
    priv->num_bins = (cl_uint) bins;
    priv->spectrum_req = in_req;
    priv->have_spectrum_req = TRUE;
    num_pixels = (cl_ulong) nx * (cl_ulong) in_req.dims[1];
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    queue = ufo_gpu_node_get_cmd_queue (node);
    if (!configure_execution (priv, queue, num_pixels)) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                     "frc-core cannot configure ring reduction for the selected device");
        return;
    }
    requisition->n_dims = 2;
    requisition->dims[0] = bins;
    requisition->dims[1] = 7;
}

static guint
ufo_frc_core_task_get_num_inputs (UfoTask *task)
{
    (void) task;
    return 1;
}

static guint
ufo_frc_core_task_get_num_dimensions (UfoTask *task,
                                      guint input)
{
    (void) task;
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_frc_core_task_get_mode (UfoTask *task)
{
    (void) task;
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_GPU;
}

static UfoNode *
ufo_frc_core_task_copy_real (UfoNode *node,
                             GError **error)
{
    (void) node;
    g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_COPY,
                 "frc-core cannot be copied; disable graph expansion and select one GPU");
    return NULL;
}

static gboolean
ufo_frc_core_task_equal_real (UfoNode *n1,
                              UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_FRC_CORE_TASK (n1) && UFO_IS_FRC_CORE_TASK (n2), FALSE);
    return n1 == n2;
}

static gboolean
configure_execution (UfoFrcCoreTaskPrivate *priv,
                     cl_command_queue queue,
                     cl_ulong num_pixels)
{
    cl_device_id device;
    cl_uint compute_units;
    cl_ulong device_local_memory;
    cl_ulong kernel_local_memory;
    size_t device_max_group_size;
    size_t kernel_max_group_size;
    size_t preferred_multiple;
    size_t maximum_groups;
    gsize local_bytes = 0;
    gsize local_items;
    gsize partial_count;
    gsize partial_size;
    cl_int error;

    error = clGetCommandQueueInfo (
        queue, CL_QUEUE_DEVICE, sizeof (cl_device_id), &device, NULL);
    if (error != CL_SUCCESS) {
        UFO_RESOURCES_CHECK_CLERR (error);
        return FALSE;
    }
    error = clGetDeviceInfo (
        device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof (cl_uint), &compute_units, NULL);
    if (error != CL_SUCCESS) {
        UFO_RESOURCES_CHECK_CLERR (error);
        return FALSE;
    }
    error = clGetDeviceInfo (
        device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof (cl_ulong), &device_local_memory, NULL);
    if (error != CL_SUCCESS) {
        UFO_RESOURCES_CHECK_CLERR (error);
        return FALSE;
    }
    error = clGetDeviceInfo (
        device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof (size_t), &device_max_group_size, NULL);
    if (error != CL_SUCCESS) {
        UFO_RESOURCES_CHECK_CLERR (error);
        return FALSE;
    }
    error = clGetKernelWorkGroupInfo (
        priv->accumulate_kernel, device, CL_KERNEL_WORK_GROUP_SIZE,
        sizeof (size_t), &kernel_max_group_size, NULL);
    if (error != CL_SUCCESS) {
        UFO_RESOURCES_CHECK_CLERR (error);
        return FALSE;
    }
    error = clGetKernelWorkGroupInfo (
        priv->accumulate_kernel, device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,
        sizeof (size_t), &preferred_multiple, NULL);
    if (error != CL_SUCCESS) {
        UFO_RESOURCES_CHECK_CLERR (error);
        return FALSE;
    }
    error = clGetKernelWorkGroupInfo (
        priv->accumulate_kernel, device, CL_KERNEL_LOCAL_MEM_SIZE,
        sizeof (cl_ulong), &kernel_local_memory, NULL);
    if (error != CL_SUCCESS) {
        UFO_RESOURCES_CHECK_CLERR (error);
        return FALSE;
    }

    if (!checked_mul_size ((gsize) priv->num_bins, 6, &local_items) ||
        !checked_mul_size (local_items, sizeof (cl_uint), &local_bytes) ||
        (cl_ulong) local_bytes + kernel_local_memory > device_local_memory) {
        g_warning ("frc-core needs %lu bytes of dynamic local memory for %u bins, "
                   "but the selected device has %lu bytes available",
                   (gulong) local_bytes, priv->num_bins,
                   (gulong) device_local_memory);
        return FALSE;
    }

    priv->local_size = MIN ((size_t) 256,
                            MIN (device_max_group_size, kernel_max_group_size));
    if (preferred_multiple > 1 && priv->local_size >= preferred_multiple)
        priv->local_size -= priv->local_size % preferred_multiple;
    if (priv->local_size == 0)
        priv->local_size = 1;

    maximum_groups = (size_t) ((num_pixels + priv->local_size - 1) / priv->local_size);
    priv->num_groups = (cl_uint) MAX ((size_t) 1,
        MIN ((size_t) compute_units * 4, maximum_groups));

    if (!checked_mul_size ((gsize) priv->num_groups, (gsize) priv->num_bins, &partial_count) ||
        !checked_mul_size (partial_count, sizeof (cl_float8), &partial_size)) {
        g_warning ("frc-core partial-buffer size overflow");
        return FALSE;
    }

    if (priv->partials != NULL && priv->partials_size != partial_size) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->partials));
        priv->partials = NULL;
    }

    if (priv->partials == NULL) {
        priv->partials = clCreateBuffer (priv->context, CL_MEM_READ_WRITE,
                                         partial_size, NULL, &error);
        if (error != CL_SUCCESS || priv->partials == NULL) {
            UFO_RESOURCES_CHECK_CLERR (error);
            return FALSE;
        }
        priv->partials_size = partial_size;
    }

    priv->execution_configured = TRUE;
    return TRUE;
}

static gboolean
ufo_frc_core_task_process (UfoTask *task,
                           UfoBuffer **inputs,
                           UfoBuffer *output,
                           UfoRequisition *requisition)
{
    UfoFrcCoreTaskPrivate *priv = UFO_FRC_CORE_TASK_GET_PRIVATE (task);
    UfoRequisition current_req;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue queue;
    cl_mem input_mem;
    cl_mem first_mem;
    cl_mem output_mem;
    cl_uint nx;
    cl_uint ny;
    cl_ulong num_pixels;
    cl_float delta_kx;
    cl_float delta_ky;
    size_t global_size;
    size_t reduce_size;
    gsize local_bytes;

    (void) requisition;

    ufo_buffer_get_requisition (inputs[0], &current_req);

    if (!same_requisition (&current_req, &priv->spectrum_req) ||
        ufo_buffer_get_layout (inputs[0]) != UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED) {
        g_warning ("frc-core discarded an incompatible spectrum; all pair members "
                   "must be complex and have the same shape");
        reset_pair (priv);
        return FALSE;
    }

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    queue = ufo_gpu_node_get_cmd_queue (node);
    input_mem = ufo_buffer_get_device_array (inputs[0], queue);

    if (!priv->have_first) {
        if (priv->first_spectrum == NULL)
            priv->first_spectrum = ufo_buffer_dup (inputs[0]);
        else if (ufo_buffer_cmp_dimensions (priv->first_spectrum, &current_req) != 0)
            ufo_buffer_resize (priv->first_spectrum, &current_req);

        first_mem = ufo_buffer_get_device_array (priv->first_spectrum, queue);
        ufo_buffer_set_layout (priv->first_spectrum, UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED);
        UFO_RESOURCES_CHECK_CLERR (clEnqueueCopyBuffer (
            queue, input_mem, first_mem, 0, 0, ufo_buffer_get_size (inputs[0]),
            0, NULL, NULL));
        priv->have_first = TRUE;
        priv->result_ready = FALSE;
        priv->generated = FALSE;
        return TRUE;
    }

    nx = (cl_uint) (current_req.dims[0] / 2);
    ny = (cl_uint) current_req.dims[1];
    num_pixels = (cl_ulong) nx * (cl_ulong) ny;
    delta_kx = (cl_float) (1.0 / ((gdouble) nx * priv->pixel_size_x));
    delta_ky = (cl_float) (1.0 / ((gdouble) ny * priv->pixel_size_y));

    if (!priv->execution_configured && !configure_execution (priv, queue, num_pixels)) {
        reset_pair (priv);
        return FALSE;
    }

    first_mem = ufo_buffer_get_device_array (priv->first_spectrum, queue);
    output_mem = ufo_buffer_get_device_array (output, queue);
    ufo_buffer_set_layout (output, UFO_BUFFER_LAYOUT_REAL);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    local_bytes = (gsize) priv->num_bins * 6 * sizeof (cl_uint);
    global_size = (size_t) priv->num_groups * priv->local_size;

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 0, sizeof (cl_mem), &first_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 1, sizeof (cl_mem), &input_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 2, sizeof (cl_mem), &priv->partials));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 3, local_bytes, NULL));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 4, sizeof (cl_uint), &nx));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 5, sizeof (cl_uint), &ny));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 6, sizeof (cl_float), &delta_kx));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 7, sizeof (cl_float), &delta_ky));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 8, sizeof (cl_float), &priv->resolved_ring_width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 9, sizeof (cl_uint), &priv->num_bins));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->accumulate_kernel, 10, sizeof (cl_ulong), &num_pixels));
    ufo_profiler_call (profiler, queue, priv->accumulate_kernel, 1,
                       &global_size, &priv->local_size);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->reduce_kernel, 0, sizeof (cl_mem), &priv->partials));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->reduce_kernel, 1, sizeof (cl_mem), &output_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->reduce_kernel, 2, sizeof (cl_uint), &priv->num_groups));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->reduce_kernel, 3, sizeof (cl_uint), &priv->num_bins));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->reduce_kernel, 4, sizeof (cl_float), &priv->resolved_ring_width));
    reduce_size = (size_t) priv->num_bins;
    ufo_profiler_call (profiler, queue, priv->reduce_kernel, 1,
                       &reduce_size, NULL);

    priv->have_first = FALSE;
    priv->result_ready = TRUE;
    priv->generated = FALSE;
    return FALSE;
}

static gboolean
ufo_frc_core_task_generate (UfoTask *task,
                            UfoBuffer *output,
                            UfoRequisition *requisition)
{
    UfoFrcCoreTaskPrivate *priv = UFO_FRC_CORE_TASK_GET_PRIVATE (task);

    (void) output;
    (void) requisition;

    if (priv->result_ready && !priv->generated) {
        priv->generated = TRUE;
        return TRUE;
    }

    if (priv->result_ready && priv->generated) {
        priv->result_ready = FALSE;
        priv->generated = FALSE;
        return FALSE;
    }

    if (priv->inputs_stopped && priv->have_first && !priv->incomplete_warned) {
        g_warning ("frc-core received an incomplete final spectrum pair; no result was emitted");
        priv->incomplete_warned = TRUE;
        priv->have_first = FALSE;
    }

    return FALSE;
}

static void
ufo_frc_core_task_inputs_stopped (UfoTask *task)
{
    UFO_FRC_CORE_TASK_GET_PRIVATE (task)->inputs_stopped = TRUE;
}

static void
ufo_frc_core_task_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
    UfoFrcCoreTaskPrivate *priv = UFO_FRC_CORE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_PIXEL_SIZE_X:
            priv->pixel_size_x = g_value_get_double (value);
            break;
        case PROP_PIXEL_SIZE_Y:
            priv->pixel_size_y = g_value_get_double (value);
            break;
        case PROP_RING_WIDTH:
            priv->ring_width = g_value_get_double (value);
            break;
        case PROP_MAX_FREQUENCY:
            priv->max_frequency = g_value_get_double (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_frc_core_task_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
    UfoFrcCoreTaskPrivate *priv = UFO_FRC_CORE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_PIXEL_SIZE_X:
            g_value_set_double (value, priv->pixel_size_x);
            break;
        case PROP_PIXEL_SIZE_Y:
            g_value_set_double (value, priv->pixel_size_y);
            break;
        case PROP_RING_WIDTH:
            g_value_set_double (value, priv->ring_width);
            break;
        case PROP_MAX_FREQUENCY:
            g_value_set_double (value, priv->max_frequency);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_frc_core_task_finalize (GObject *object)
{
    UfoFrcCoreTaskPrivate *priv = UFO_FRC_CORE_TASK_GET_PRIVATE (object);

    if (priv->partials != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->partials));
        priv->partials = NULL;
    }
    if (priv->first_spectrum != NULL) {
        g_object_unref (priv->first_spectrum);
        priv->first_spectrum = NULL;
    }
    if (priv->accumulate_kernel != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->accumulate_kernel));
        priv->accumulate_kernel = NULL;
    }
    if (priv->reduce_kernel != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->reduce_kernel));
        priv->reduce_kernel = NULL;
    }
    if (priv->context != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    G_OBJECT_CLASS (ufo_frc_core_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_frc_core_task_setup;
    iface->get_num_inputs = ufo_frc_core_task_get_num_inputs;
    iface->get_num_dimensions = ufo_frc_core_task_get_num_dimensions;
    iface->get_mode = ufo_frc_core_task_get_mode;
    iface->get_requisition = ufo_frc_core_task_get_requisition;
    iface->process = ufo_frc_core_task_process;
    iface->generate = ufo_frc_core_task_generate;
}

static void
ufo_frc_core_task_class_init (UfoFrcCoreTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    UfoNodeClass *node_class = UFO_NODE_CLASS (klass);

    oclass->set_property = ufo_frc_core_task_set_property;
    oclass->get_property = ufo_frc_core_task_get_property;
    oclass->finalize = ufo_frc_core_task_finalize;

    properties[PROP_PIXEL_SIZE_X] =
        g_param_spec_double ("pixel-size-x", "Pixel size X",
            "Pixel spacing along the fastest-varying X dimension",
            0.0, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE);
    properties[PROP_PIXEL_SIZE_Y] =
        g_param_spec_double ("pixel-size-y", "Pixel size Y",
            "Pixel spacing along the Y dimension",
            0.0, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE);
    properties[PROP_RING_WIDTH] =
        g_param_spec_double ("ring-width", "Ring width",
            "Physical radial ring width; zero selects the coarsest axial frequency increment",
            0.0, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE);
    properties[PROP_MAX_FREQUENCY] =
        g_param_spec_double ("max-frequency", "Maximum frequency",
            "Exclusive radial frequency limit; zero selects the smallest axial Nyquist frequency",
            0.0, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE);

    for (guint property = PROP_0 + 1; property < N_PROPERTIES; property++)
        g_object_class_install_property (oclass, property, properties[property]);

    node_class->copy = ufo_frc_core_task_copy_real;
    node_class->equal = ufo_frc_core_task_equal_real;

    g_type_class_add_private (oclass, sizeof (UfoFrcCoreTaskPrivate));
}

static void
ufo_frc_core_task_init (UfoFrcCoreTask *self)
{
    UfoFrcCoreTaskPrivate *priv;

    self->priv = priv = UFO_FRC_CORE_TASK_GET_PRIVATE (self);
    priv->context = NULL;
    priv->accumulate_kernel = NULL;
    priv->reduce_kernel = NULL;
    priv->partials = NULL;
    priv->partials_size = 0;
    priv->first_spectrum = NULL;
    priv->pixel_size_x = 0.0;
    priv->pixel_size_y = 0.0;
    priv->ring_width = 0.0;
    priv->max_frequency = 0.0;
    priv->resolved_ring_width = 0.0f;
    priv->num_bins = 0;
    priv->num_groups = 0;
    priv->local_size = 0;
    priv->execution_configured = FALSE;
    priv->have_spectrum_req = FALSE;
    priv->have_first = FALSE;
    priv->result_ready = FALSE;
    priv->generated = FALSE;
    priv->inputs_stopped = FALSE;
    priv->incomplete_warned = FALSE;
    g_signal_connect (self, "inputs_stopped",
                      (GCallback) ufo_frc_core_task_inputs_stopped, NULL);
}
