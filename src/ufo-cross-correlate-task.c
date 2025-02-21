/*
 * Copyright (C) 2011-2025 Karlsruhe Institute of Technology
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
#include <math.h>

#include "ufo-cross-correlate-task.h"
#include "common/ufo-math.h"
#include "common/ufo-fft.h"


typedef enum {
    CROSS_CORR_ALIGN,
    CROSS_CORR_OUTPUT,
    CROSS_CORR_PICK_CLOSEST
} Postproc;

static GEnumValue postproc_values[] = {
    { CROSS_CORR_ALIGN,         "CROSS_CORR_ALIGN",         "align" },
    { CROSS_CORR_OUTPUT,        "CROSS_CORR_OUTPUT",        "output" },
    { CROSS_CORR_PICK_CLOSEST,  "CROSS_CORR_PICK_CLOSEST",  "pick-closest" },
    { 0, NULL, NULL}
};

struct _UfoCrossCorrelateTaskPrivate {
    guint num_inputs, supersampling, nth;
    gfloat *dx, *dy;
    cl_context context;
    cl_kernel argmax_kernel, pack_kernel, crosscorr_kernel, modulation_kernel, idft_kernel, sum_kernel;
    cl_mem max_mem, argmax_mem, slice_mem, sum_mem;
    gfloat gauss_sigma;
    gboolean apply_laplace;
    UfoFft *fft;
    UfoFftParameter param;
    UfoBuffer *tmp_buffer_cplx, *tmp_buffer_real;
    Postproc postproc;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoCrossCorrelateTask, ufo_cross_correlate_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_CROSS_CORRELATE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_CROSS_CORRELATE_TASK, UfoCrossCorrelateTaskPrivate))

enum {
    PROP_0,
    PROP_NUM_INPUTS,
    PROP_NTH,
    PROP_POSTPROC,
    PROP_SUPERSAMPLING,
    PROP_GAUSS_SIGMA,
    PROP_APPLY_LAPLACE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_cross_correlate_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_CROSS_CORRELATE_TASK, NULL));
}

static void
ufo_cross_correlate_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoCrossCorrelateTaskPrivate *priv = UFO_CROSS_CORRELATE_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    priv->argmax_kernel = ufo_resources_get_kernel (resources, "reductor.cl", "parallel_argmax", NULL, error);
    priv->pack_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_pack", NULL, error);
    priv->modulation_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_modulate_xy", NULL, error);
    priv->idft_kernel = ufo_resources_get_kernel (resources, "complex.cl", "crosscorr_idft_2", NULL, error);
    priv->crosscorr_kernel = ufo_resources_get_kernel (resources, "complex.cl", "c_crosscorr", NULL, error);
    priv->sum_kernel = ufo_resources_get_kernel (resources, "reductor.cl", "reduce_M_SUM", NULL, error);

    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainContext (priv->context), error);

    if (priv->argmax_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->argmax_kernel), error);
    }
    if (priv->pack_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->pack_kernel), error);
    }
    if (priv->crosscorr_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->crosscorr_kernel), error);
    }
    if (priv->modulation_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->modulation_kernel), error);
    }
    if (priv->idft_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->idft_kernel), error);
    }
    if (priv->sum_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->sum_kernel), error);
    }

    if (priv->tmp_buffer_cplx == NULL) {
        UfoRequisition requisition;
        requisition.n_dims = 2;
        requisition.dims[0] = 1;
        requisition.dims[1] = 1;

        priv->tmp_buffer_cplx = ufo_buffer_new(&requisition, priv->context);
        ufo_buffer_set_layout (priv->tmp_buffer_cplx, UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED);
        priv->tmp_buffer_real = ufo_buffer_new(&requisition, priv->context);
    }
}

static void
ufo_cross_correlate_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition,
                                 GError **error)
{
    UfoCrossCorrelateTaskPrivate *priv;
    UfoRequisition in_req, tmp_req;
    cl_command_queue queue;
    gsize num_images = 0;
    guint num_processed;

    priv = UFO_CROSS_CORRELATE_TASK_GET_PRIVATE (task);
    g_object_get (task, "num_processed", &num_processed, NULL);
    queue = ufo_gpu_node_get_cmd_queue (UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task))));
    ufo_buffer_get_requisition (inputs[0], &in_req);

    /* Inputs may be 2D or 3D, check width, height and determine number of
     * output images */
    for (int i = 0; i < priv->num_inputs; i++) {
        ufo_buffer_get_requisition (inputs[i], &tmp_req);
        if (tmp_req.dims[0] != in_req.dims[0] || tmp_req.dims[1] != tmp_req.dims[1]) {
            g_set_error_literal (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                             "cross-correlate inputs must have the same width and height");
            return;
        }

        for (int j = 0; j < 2; j++) {
            if (ufo_math_compute_closest_smaller_power_of_2 (tmp_req.dims[j]) != tmp_req.dims[j]) {
                g_set_error_literal (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                                 "cross-correlate input dimensions must be powers of 2");
                return;
            }
        }
        if (ufo_buffer_get_layout (inputs[i]) != UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED) {
            g_set_error_literal (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                                 "cross-correlate input must be complex");
            return;
        }
        if (!num_processed) {
            g_log (
                "cross-corr", G_LOG_LEVEL_DEBUG, "in requisition %d: (%lu, %lu, %lu, ndims=%u)",
                i, tmp_req.dims[0], tmp_req.dims[1], tmp_req.n_dims > 2 ? tmp_req.dims[2] : 1, tmp_req.n_dims);
        }
        for (int j = 0; j < (tmp_req.n_dims > 2 ? tmp_req.dims[2] : 1); j++) {
            num_images++;
        }
    }

    /* Setup output requisition */
    requisition->n_dims = 3;
    if (priv->postproc == CROSS_CORR_OUTPUT) {
        /* Output is real not complex */
        requisition->dims[0] = in_req.dims[0] / 2;
        requisition->dims[1] = in_req.dims[1];
    } else {
        requisition->dims[0] = in_req.dims[0];
        requisition->dims[1] = in_req.dims[1];
    }
    if (priv->postproc == CROSS_CORR_PICK_CLOSEST) {
        /* Only the first input and the "best" are on output */
        requisition->dims[2] = 2;
    } else {
        requisition->dims[2] = num_images;
    }

    /* Remember dx, dy in case `nth` is specified */
    if (priv->dx == NULL) {
        priv->dx = (gfloat *) g_malloc (sizeof (gfloat) * requisition->dims[2]);
    }
    if (priv->dy == NULL) {
        priv->dy = (gfloat *) g_malloc (sizeof (gfloat) * requisition->dims[2]);
    }

    if (!num_processed) {
        g_log ("cross-corr", G_LOG_LEVEL_DEBUG, "out requisition (%lu, %lu, %lu, ndims=%u)",
               requisition->dims[0], requisition->dims[1], requisition->dims[2], requisition->n_dims);
    }

    /* FFT parameter takes the real width */
    priv->param.size[0] = in_req.dims[0] / 2;
    priv->param.size[1] = in_req.dims[1];
    priv->param.size[2] = 1;
    priv->param.batch = 1;

    UFO_RESOURCES_CHECK_SET_AND_RETURN (ufo_fft_update (priv->fft, priv->context, queue, &priv->param), error);
}

static guint
ufo_cross_correlate_task_get_num_inputs (UfoTask *task)
{
    UfoCrossCorrelateTaskPrivate *priv = UFO_CROSS_CORRELATE_TASK_GET_PRIVATE (task);

    return priv->num_inputs;
}

static guint
ufo_cross_correlate_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_cross_correlate_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

/*
 * Computes maximum and arg maximum of cross-correlation result.
 */
static gsize
compute_argmax (UfoCrossCorrelateTaskPrivate *priv,
                UfoProfiler *profiler,
                cl_command_queue cmd_queue,
                cl_mem input,
                gsize real_size,
                gsize work_group_size,
                gfloat *ret_maximum)
{
    cl_float *max_blocks;
    gsize global_work_size, num_groups, i;
    cl_ulong *argmax_blocks;
    cl_int cl_error;
    cl_int pixels_per_thread;
    gfloat maximum = -INFINITY;
    gsize argmax;

    num_groups = (real_size - 1) / work_group_size + 1;
    pixels_per_thread = MAX (32, ufo_math_compute_closest_smaller_power_of_2 ((gsize) ceil (sqrt (num_groups))));
    num_groups = (num_groups - 1) / pixels_per_thread + 1;
    global_work_size = num_groups * work_group_size;
    g_debug ("cross correlate argmax: real size=%lu local size=%lu global size=%lu, pixels per thread=%d",
             real_size, work_group_size, global_work_size, pixels_per_thread);
    /* Number of output points (every work group produces 1 output value) */
    max_blocks = (cl_float *) g_malloc (num_groups * sizeof (cl_float));
    argmax_blocks = (cl_ulong *) g_malloc (num_groups * sizeof (cl_ulong));

    if (!priv->max_mem) {
        priv->max_mem = clCreateBuffer (priv->context,
                                        CL_MEM_WRITE_ONLY,
                                        num_groups * sizeof (cl_float),
                                        NULL,
                                        &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }
    if (!priv->argmax_mem) {
        priv->argmax_mem = clCreateBuffer (priv->context,
                                           CL_MEM_WRITE_ONLY,
                                           num_groups * sizeof (cl_ulong),
                                           NULL,
                                           &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->argmax_kernel, 0, sizeof (cl_mem), &input));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->argmax_kernel, 1, sizeof (cl_mem), &priv->max_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->argmax_kernel, 2, sizeof (cl_mem), &priv->argmax_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->argmax_kernel, 3, sizeof (cl_float) * work_group_size, NULL));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->argmax_kernel, 4, sizeof (cl_ulong) * work_group_size, NULL));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->argmax_kernel, 5, sizeof (cl_ulong), &real_size));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->argmax_kernel, 6, sizeof (cl_int), &pixels_per_thread));
    ufo_profiler_call (profiler, cmd_queue, priv->argmax_kernel, 1, &global_work_size, &work_group_size);
    UFO_RESOURCES_CHECK_CLERR (clEnqueueReadBuffer (cmd_queue,
                                                    priv->max_mem,
                                                    CL_TRUE,
                                                    0,
                                                    num_groups * sizeof (cl_float),
                                                    (void *) max_blocks,
                                                    0, NULL, NULL));
    UFO_RESOURCES_CHECK_CLERR (clEnqueueReadBuffer (cmd_queue,
                                                    priv->argmax_mem,
                                                    CL_TRUE,
                                                    0,
                                                    num_groups * sizeof (cl_ulong),
                                                    (void *) argmax_blocks,
                                                    0, NULL, NULL));

    for (i = 0; i < num_groups; i++) {
        if (max_blocks[i] > maximum) {
            maximum = max_blocks[i];
            argmax = argmax_blocks[i];
        }
    }
    *ret_maximum = maximum;

    g_free (max_blocks);
    g_free (argmax_blocks);

    return argmax;
}

/*
 * Helper function for refine_result below. Here we perform the summation part of IDFT2
 * (inverse discrete Fourerier transform in 2D).
 */
static gfloat
compute_mean (UfoCrossCorrelateTaskPrivate *priv,
              UfoProfiler *profiler,
              cl_command_queue cmd_queue,
              cl_mem input,
              gsize real_size,
              gsize work_group_size)
{
    cl_float *summed_blocks;
    gsize global_work_size, num_groups, i;
    cl_int cl_error;
    cl_int pixels_per_thread;
    gfloat sum = 0.0f;

    num_groups = (real_size - 1) / work_group_size + 1;
    pixels_per_thread = MAX (32, ufo_math_compute_closest_smaller_power_of_2 ((gsize) ceil (sqrt (num_groups))));
    num_groups = (num_groups - 1) / pixels_per_thread + 1;
    global_work_size = num_groups * work_group_size;
    /* Number of output points (every work group produces 1 output value) */
    summed_blocks = (cl_float *) g_malloc (num_groups * sizeof (cl_float));

    if (!priv->sum_mem) {
        priv->sum_mem = clCreateBuffer (priv->context,
                                        CL_MEM_WRITE_ONLY,
                                        num_groups * sizeof (cl_float),
                                        NULL,
                                        &cl_error);
        UFO_RESOURCES_CHECK_CLERR (cl_error);
    }

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sum_kernel, 0, sizeof (cl_mem), &input));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sum_kernel, 1, sizeof (cl_mem), &priv->sum_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sum_kernel, 2, sizeof (cl_mem), NULL));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sum_kernel, 3, sizeof (cl_float) * work_group_size, NULL));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sum_kernel, 4, sizeof (cl_ulong), &real_size));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->sum_kernel, 5, sizeof (cl_int), &pixels_per_thread));
    ufo_profiler_call (profiler, cmd_queue, priv->sum_kernel, 1, &global_work_size, &work_group_size);
    UFO_RESOURCES_CHECK_CLERR (clEnqueueReadBuffer (cmd_queue,
                                                    priv->sum_mem,
                                                    CL_TRUE,
                                                    0,
                                                    num_groups * sizeof (float),
                                                    (void *) summed_blocks,
                                                    0, NULL, NULL));

    for (i = 0; i < num_groups; i++) {
        sum += summed_blocks[i];
    }

    g_free (summed_blocks);

    return sum / real_size;
}

/*
 * Compute higher-resolution cross-correlation by directly evaluating IDFT2 at
 * non-integer points, which is the same as supersampling by padding
 * the Fourier space by the amount of shape * supersampling.
 * The evaluation is done around the found low resolution peak, e.g.
 * supersampling 2 and lowres_dx 100 looks at 99.5 and 100.5, supersampling 4
 * around 99.5, 99.75, 100.25, 100.5 and so on.
 */
static void
refine_result (UfoCrossCorrelateTaskPrivate *priv,
               gfloat *lowres_dx,
               gfloat *lowres_dy,
               gfloat *maximum,
               UfoProfiler *profiler,
               cl_command_queue cmd_queue,
               cl_mem reference_mem,
               cl_mem in_mem,
               cl_mem tmp_mem_real,
               gsize real_size,
               gsize work_group_size)
{
    cl_float x, y, max_x, max_y;
    cl_int apply_laplace = (gint) priv->apply_laplace;
    gfloat mean, max_mean = -G_MAXFLOAT;

    for (gint i = - ((gint) priv->supersampling / 2); i <= (gint) priv->supersampling / 2; i++) {
        /* lowres_dx is the shift, so we need a minus, same for lowres_dy */
        x = -(*lowres_dx) + ((float) i) / priv->supersampling;
        for (gint j = - ((gint) priv->supersampling / 2); j <= (gint) priv->supersampling / 2; j++) {
            y = -(*lowres_dy) + ((float) j) / priv->supersampling;
            if (i == 0 && j == 0) {
                /* This is the low resolution result which we have already */
                continue;
            }
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->idft_kernel, 0, sizeof (cl_mem), (gpointer) &reference_mem));
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->idft_kernel, 1, sizeof (cl_mem), (gpointer) &in_mem));
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->idft_kernel, 2, sizeof (cl_float), &x));
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->idft_kernel, 3, sizeof (cl_float), &y));
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->idft_kernel, 4, sizeof (cl_float), &priv->gauss_sigma));
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->idft_kernel, 5, sizeof (cl_int), &apply_laplace));
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->idft_kernel, 6, sizeof (cl_mem), (gpointer) &tmp_mem_real));
            ufo_profiler_call (profiler, cmd_queue, priv->idft_kernel, 2, priv->param.size, NULL);
            mean = compute_mean (
                priv,
                profiler,
                cmd_queue,
                tmp_mem_real,
                real_size,
                work_group_size
            );
            if (mean > max_mean) {
                max_mean = mean;
                max_x = x;
                max_y = y;
            }
        }
    }

    if (max_mean > *maximum) {
        /* Update results only if there is a higher peak in the supersampled result.
         * Convert peak position to shift, hence the minuses. */
        *lowres_dx = -max_x;
        *lowres_dy = -max_y;
        *maximum = max_mean;
    }
}


static gboolean
ufo_cross_correlate_task_process (UfoTask *task,
                                  UfoBuffer **inputs,
                                  UfoBuffer *output,
                                  UfoRequisition *requisition)
{
    UfoCrossCorrelateTaskPrivate *priv;
    UfoProfiler *profiler;
    gsize work_group_size;
    cl_command_queue cmd_queue;
    cl_mem reference_mem, tmp_mem_cplx, tmp_mem_real, in_mem;
    guint8 *out_mem, *tmp_mem_host, *in_host_mem;
    UfoGpuNode *node;
    gsize argmax, slice_size;
    UfoRequisition in_req, real_req, cplx_req;
    cl_int cl_err, width, height, apply_laplace, false_value = 0;
    cl_float scale, dx, dy, global_dx, global_dy;
    gsize small_global_work_size[3];
    gint output_index = 0;
    gfloat maximum, global_maximum = -G_MAXFLOAT;
    gint global_i, global_j;
    gboolean copy_host;
    guint num_processed;

    priv = UFO_CROSS_CORRELATE_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    work_group_size = g_value_get_ulong (ufo_gpu_node_get_info (node, UFO_GPU_NODE_INFO_MAX_WORK_GROUP_SIZE));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    g_object_get (task, "num_processed", &num_processed, NULL);

    out_mem = (guint8 *) ufo_buffer_get_host_array (output, NULL);
    reference_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    ufo_buffer_get_requisition (inputs[0], &in_req);
    width = in_req.dims[0] / 2;
    height = in_req.dims[1];
    scale = 1.0f / (width * height);

    cplx_req.n_dims = 2;
    cplx_req.dims[0] = in_req.dims[0];
    cplx_req.dims[1] = in_req.dims[1];
    cplx_req.dims[2] = 1;

    real_req.n_dims = 2;
    real_req.dims[0] = width;
    real_req.dims[1] = height;
    real_req.dims[2] = 1;

    small_global_work_size[0] = in_req.dims[0] / 2;
    small_global_work_size[1] = in_req.dims[1];
    small_global_work_size[2] = 1;

    apply_laplace = (gint) priv->apply_laplace;
    if (ufo_buffer_cmp_dimensions (priv->tmp_buffer_cplx, &cplx_req) != 0) {
        ufo_buffer_resize (priv->tmp_buffer_cplx, &cplx_req);
    }
    if (ufo_buffer_cmp_dimensions (priv->tmp_buffer_real, &real_req) != 0) {
        ufo_buffer_resize (priv->tmp_buffer_real, &real_req);
    }
    if (priv->slice_mem == NULL) {
        priv->slice_mem = clCreateBuffer (priv->context,
                                          CL_MEM_READ_WRITE,
                                          in_req.dims[0] * in_req.dims[1] * sizeof (cl_float),
                                          NULL,
                                          &cl_err);
        UFO_RESOURCES_CHECK_CLERR (cl_err);
    }
    if (priv->postproc == CROSS_CORR_OUTPUT) {
        slice_size = ufo_buffer_get_size (priv->tmp_buffer_real);
        ufo_buffer_set_layout (output, UFO_BUFFER_LAYOUT_REAL);
    } else {
        slice_size = ufo_buffer_get_size (priv->tmp_buffer_cplx);
        ufo_buffer_set_layout (output, UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED);
    }

    for (int i = 0; i < priv->num_inputs; i++) {
        ufo_buffer_get_requisition (inputs[i], &in_req);
        if (in_req.n_dims == 2 || (in_req.n_dims && in_req.dims[2] == 1)) {
            in_mem = ufo_buffer_get_device_array (inputs[i], cmd_queue);
            copy_host = FALSE;
        } else {
            /* Get host memory because there can be many input images in one 3D buffer */
            in_host_mem = (guint8 *) ufo_buffer_get_host_array (inputs[i], NULL);
            in_mem = priv->slice_mem;
            copy_host = TRUE;
        }
        for (int j = 0; j < (in_req.n_dims > 2 ? in_req.dims[2] : 1); j++) {
            tmp_mem_cplx = ufo_buffer_get_device_array (priv->tmp_buffer_cplx, cmd_queue);
            tmp_mem_real = ufo_buffer_get_device_array (priv->tmp_buffer_real, cmd_queue);

            if (copy_host) {
                /* Host -> device copy */
                cl_err = clEnqueueWriteBuffer (cmd_queue,
                                               in_mem,
                                               CL_TRUE,
                                               0,
                                               in_req.dims[0] * in_req.dims[1] * sizeof (cl_float),
                                               in_host_mem + j * in_req.dims[0] * in_req.dims[1] * sizeof (cl_float),
                                               0, NULL, NULL);

                UFO_RESOURCES_CHECK_CLERR (cl_err);
            }

            if (num_processed % priv->nth == 0) {
                /* Recompute cross-correlation result, dx and dy only every *nth* images */
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->crosscorr_kernel, 0, sizeof (cl_mem), (gpointer) &reference_mem));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->crosscorr_kernel, 1, sizeof (cl_mem), (gpointer) &in_mem));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->crosscorr_kernel, 2, sizeof (cl_float), &priv->gauss_sigma));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->crosscorr_kernel, 3, sizeof (cl_int), &apply_laplace));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->crosscorr_kernel, 4, sizeof (cl_mem), (gpointer) &tmp_mem_cplx));
                ufo_profiler_call (profiler, cmd_queue, priv->crosscorr_kernel, 2, priv->param.size, NULL);

                UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (priv->fft, cmd_queue, profiler, tmp_mem_cplx, tmp_mem_cplx,
                                                            UFO_FFT_BACKWARD, 0, NULL, NULL));

                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 0, sizeof (cl_mem), (gpointer) &tmp_mem_cplx));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 1, sizeof (cl_mem), (gpointer) &tmp_mem_real));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 2, sizeof (cl_int), &width));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 3, sizeof (cl_int), &height));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 4, sizeof (cl_float), &scale));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 5, sizeof (cl_int), &false_value));
                ufo_profiler_call (profiler, cmd_queue, priv->pack_kernel, 2, priv->param.size, NULL);

                argmax = compute_argmax (
                    priv,
                    profiler,
                    cmd_queue,
                    tmp_mem_real,
                    width * height,
                    work_group_size,
                    &maximum
                );
                dx = - (float) (argmax % width);
                dy = - (float) (argmax / width);

                if (priv->postproc != CROSS_CORR_OUTPUT) {
                    /* No point in refining the result if the cross-correlation output is desired, */
                    /* we do not provide the full high resolution version. */
                    if (priv->supersampling > 1) {
                        refine_result (
                            priv,
                            &dx,
                            &dy,
                            &maximum,
                            profiler,
                            cmd_queue,
                            reference_mem,
                            in_mem,
                            tmp_mem_real,
                            width * height,
                            work_group_size
                        );
                    }
                }
                if (priv->postproc == CROSS_CORR_ALIGN) {
                    priv->dx[output_index] = dx;
                    priv->dy[output_index] = dy;
                }

                /* Remember global maximum (autocorrelation of first image excluded) */
                if ((i > 0 || (i == 0 && j > 0)) && maximum > global_maximum) {
                    global_maximum = maximum;
                    global_i = i;
                    global_j = j;
                    global_dx = dx;
                    global_dy = dy;
                }

                if (priv->postproc != CROSS_CORR_PICK_CLOSEST) {
                    g_log ("cross-corr",
                           G_LOG_LEVEL_DEBUG,
                           "Iteration %4u: input %2d/%2d: shift: (%8.3f, %8.3f), max: %g",
                           num_processed, i, j, dx, dy, maximum);
                }
            }

            if (priv->postproc == CROSS_CORR_ALIGN) {
                /* Frequency modulation = real space shift */
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->modulation_kernel, 0, sizeof (cl_mem), (gpointer) &in_mem));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->modulation_kernel, 1, sizeof (cl_mem), (gpointer) &tmp_mem_cplx));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->modulation_kernel, 2, sizeof (cl_float), &priv->dx[output_index]));
                UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->modulation_kernel, 3, sizeof (cl_float), &priv->dy[output_index]));
                ufo_profiler_call (profiler, cmd_queue, priv->modulation_kernel, 2, small_global_work_size, NULL);
                tmp_mem_host = (guint8 *) ufo_buffer_get_host_array (priv->tmp_buffer_cplx, NULL);
            } else if (priv->postproc == CROSS_CORR_OUTPUT) {
                tmp_mem_host = (guint8 *) ufo_buffer_get_host_array (priv->tmp_buffer_real, NULL);
            }

            if (priv->postproc != CROSS_CORR_PICK_CLOSEST) {
                memcpy (out_mem + output_index * slice_size, tmp_mem_host, slice_size);
            }

            output_index++;
        }
    }

    if (priv->postproc == CROSS_CORR_PICK_CLOSEST) {
        ufo_buffer_get_requisition (inputs[global_i], &in_req);
        if (in_req.n_dims == 2 || (in_req.n_dims && in_req.dims[2] == 1)) {
            in_mem = ufo_buffer_get_device_array (inputs[global_i], cmd_queue);
        } else {
            /* Get host memory because there can be many input images in one 3D buffer */
            in_mem = priv->slice_mem;
            in_host_mem = (guint8 *) ufo_buffer_get_host_array (inputs[global_i], NULL);
            /* Host -> device copy */
            cl_err = clEnqueueWriteBuffer (cmd_queue,
                                           in_mem,
                                           CL_TRUE,
                                           0,
                                           in_req.dims[0] * in_req.dims[1] * sizeof (cl_float),
                                           in_host_mem + global_j * in_req.dims[0] * in_req.dims[1] * sizeof (cl_float),
                                           0, NULL, NULL);
            UFO_RESOURCES_CHECK_CLERR (cl_err);
        }

        g_log ("cross-corr",
               G_LOG_LEVEL_DEBUG,
               "Iteration %4u: best input %2d/%2d: shift: (%8.3f, %8.3f), max: %g",
               num_processed, global_i, global_j, global_dx, global_dy, global_maximum);
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->modulation_kernel, 0, sizeof (cl_mem), (gpointer) &in_mem));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->modulation_kernel, 1, sizeof (cl_mem), (gpointer) &tmp_mem_cplx));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->modulation_kernel, 2, sizeof (cl_float), &global_dx));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->modulation_kernel, 3, sizeof (cl_float), &global_dy));
        ufo_profiler_call (profiler, cmd_queue, priv->modulation_kernel, 2, small_global_work_size, NULL);

        tmp_mem_host = (guint8 *) ufo_buffer_get_host_array (inputs[0], NULL);
        memcpy (out_mem, tmp_mem_host, slice_size);
        tmp_mem_host = (guint8 *) ufo_buffer_get_host_array (priv->tmp_buffer_cplx, NULL);
        memcpy (out_mem + slice_size, tmp_mem_host, slice_size);
    }

    return TRUE;
}


static void
ufo_cross_correlate_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoCrossCorrelateTaskPrivate *priv = UFO_CROSS_CORRELATE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_NUM_INPUTS:
            priv->num_inputs = g_value_get_uint (value);
            break;
        case PROP_NTH:
            priv->nth = g_value_get_uint (value);
            break;
        case PROP_POSTPROC:
            priv->postproc = g_value_get_enum (value);
            break;
        case PROP_SUPERSAMPLING:
            priv->supersampling = g_value_get_uint (value);
            break;
        case PROP_GAUSS_SIGMA:
            priv->gauss_sigma = g_value_get_float (value);
            break;
        case PROP_APPLY_LAPLACE:
            priv->apply_laplace = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_cross_correlate_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoCrossCorrelateTaskPrivate *priv = UFO_CROSS_CORRELATE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_NUM_INPUTS:
            g_value_set_uint (value, priv->num_inputs);
            break;
        case PROP_NTH:
            g_value_set_uint (value, priv->nth);
            break;
        case PROP_POSTPROC:
            g_value_set_enum (value, priv->postproc);
            break;
        case PROP_SUPERSAMPLING:
            g_value_set_uint (value, priv->supersampling);
            break;
        case PROP_GAUSS_SIGMA:
            g_value_set_float (value, priv->gauss_sigma);
            break;
        case PROP_APPLY_LAPLACE:
            g_value_set_boolean (value, priv->apply_laplace);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_cross_correlate_task_finalize (GObject *object)
{
    UfoCrossCorrelateTaskPrivate *priv = UFO_CROSS_CORRELATE_TASK_GET_PRIVATE (object);

    if (priv->argmax_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->argmax_kernel));
        priv->argmax_kernel = NULL;
    }

    if (priv->pack_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->pack_kernel));
        priv->pack_kernel = NULL;
    }

    if (priv->crosscorr_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->crosscorr_kernel));
        priv->crosscorr_kernel = NULL;
    }

    if (priv->modulation_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->modulation_kernel));
        priv->modulation_kernel = NULL;
    }

    if (priv->idft_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->idft_kernel));
        priv->idft_kernel = NULL;
    }

    if (priv->sum_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->sum_kernel));
        priv->sum_kernel = NULL;
    }

    if (priv->max_mem) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->max_mem));
        priv->max_mem = NULL;
    }

    if (priv->sum_mem) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->sum_mem));
        priv->sum_mem = NULL;
    }

    if (priv->argmax_mem) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->argmax_mem));
        priv->argmax_mem = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    if (priv->fft) {
        ufo_fft_destroy (priv->fft);
        priv->fft = NULL;
    }

    if (priv->tmp_buffer_cplx) {
        g_object_unref(priv->tmp_buffer_cplx);
        priv->tmp_buffer_cplx = NULL;
    }

    if (priv->tmp_buffer_real) {
        g_object_unref(priv->tmp_buffer_real);
        priv->tmp_buffer_real = NULL;
    }

    if (priv->slice_mem) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->slice_mem));
        priv->slice_mem = NULL;
    }

    if (priv->dx) {
        g_free (priv->dx);
        priv->dx = NULL;
    }

    if (priv->dy) {
        g_free (priv->dy);
        priv->dy = NULL;
    }

    G_OBJECT_CLASS (ufo_cross_correlate_task_parent_class)->finalize (object);
}

static gboolean
ufo_cross_correlate_task_equal_real (UfoNode *n1, UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_CROSS_CORRELATE_TASK (n1) && UFO_IS_CROSS_CORRELATE_TASK (n2), FALSE);

    return UFO_CROSS_CORRELATE_TASK (n1)->priv->argmax_kernel == UFO_CROSS_CORRELATE_TASK (n2)->priv->argmax_kernel;
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_cross_correlate_task_setup;
    iface->get_num_inputs = ufo_cross_correlate_task_get_num_inputs;
    iface->get_num_dimensions = ufo_cross_correlate_task_get_num_dimensions;
    iface->get_mode = ufo_cross_correlate_task_get_mode;
    iface->get_requisition = ufo_cross_correlate_task_get_requisition;
    iface->process = ufo_cross_correlate_task_process;
}

static void
ufo_cross_correlate_task_class_init (UfoCrossCorrelateTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);
    UfoNodeClass *node_class = UFO_NODE_CLASS (klass);

    oclass->set_property = ufo_cross_correlate_task_set_property;
    oclass->get_property = ufo_cross_correlate_task_get_property;
    oclass->finalize = ufo_cross_correlate_task_finalize;

    properties[PROP_NUM_INPUTS] =
        g_param_spec_uint ("num-inputs",
            "Number of inputs",
            "Number of inputs",
            2, 128, 2,
            G_PARAM_READWRITE);

    properties[PROP_NTH] =
        g_param_spec_uint ("nth",
            "Correlate evey nth image",
            "Correlate evey nth image",
            1, G_MAXUINT, 1,
            G_PARAM_READWRITE);

    properties[PROP_POSTPROC] =
        g_param_spec_enum ("postproc",
            "Postprocessing (\"align\", \"output\", \"pick-closest\")",
            "Postprocessing (\"align\", \"output\", \"pick-closest\")",
            g_enum_register_static ("ufo_cross_corr_postproc", postproc_values),
            CROSS_CORR_ALIGN,
            G_PARAM_READWRITE);

    properties[PROP_SUPERSAMPLING] =
        g_param_spec_uint ("supersampling",
            "Supersampling",
            "Supersampling",
            1, +128, 1,
            G_PARAM_READWRITE);

    properties[PROP_GAUSS_SIGMA] =
        g_param_spec_float ("gauss-sigma",
            "Sigma of the Gaussian for blurring",
            "Sigma of the Gaussian for blurring",
            0.0f, G_MAXFLOAT, 0.0f,
            G_PARAM_READWRITE);

    properties[PROP_APPLY_LAPLACE] =
        g_param_spec_boolean ("apply-laplace",
            "Apply Laplace operator to convert images to edges",
            "Apply Laplace operator to convert images to edges",
            FALSE,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->equal = ufo_cross_correlate_task_equal_real;

    g_type_class_add_private (oclass, sizeof(UfoCrossCorrelateTaskPrivate));
}

static void
ufo_cross_correlate_task_init(UfoCrossCorrelateTask *self)
{
    self->priv = UFO_CROSS_CORRELATE_TASK_GET_PRIVATE(self);
    self->priv->argmax_kernel = NULL;
    self->priv->pack_kernel = NULL;
    self->priv->crosscorr_kernel = NULL;
    self->priv->modulation_kernel = NULL;
    self->priv->idft_kernel = NULL;
    self->priv->sum_kernel = NULL;
    self->priv->num_inputs = 2;
    self->priv->nth = 1;
    self->priv->supersampling = 1;
    self->priv->postproc = CROSS_CORR_ALIGN;
    self->priv->apply_laplace = FALSE;
    self->priv->fft = ufo_fft_new ();
    self->priv->param.dimensions = UFO_FFT_2D;
    self->priv->tmp_buffer_cplx = NULL;
    self->priv->tmp_buffer_real = NULL;
    self->priv->gauss_sigma = 0.0f;
    self->priv->slice_mem = NULL;
    self->priv->sum_mem = NULL;
    self->priv->max_mem = NULL;
    self->priv->argmax_mem = NULL;
    self->priv->dx = NULL;
    self->priv->dy = NULL;
}
