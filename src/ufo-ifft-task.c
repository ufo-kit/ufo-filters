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
#include "config.h"

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-ifft-task.h"
#include "common/ufo-math.h"
#include "common/ufo-fft.h"


struct _UfoIfftTaskPrivate {
    UfoFft *fft;
    UfoFftParameter param;

    cl_context context;
    cl_kernel pack_kernel, coeffs_kernel, mul_kernel, c_mul_kernel;

    UfoBuffer *coeffs_buffer, *f_coeffs_buffer, *tmp_buffer, *tmp_buffer_2;
    gsize user_size[3], fft_work_size[3];
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoIfftTask, ufo_ifft_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_IFFT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_IFFT_TASK, UfoIfftTaskPrivate))

enum {
    PROP_0,
    PROP_DIMENSIONS,
    PROP_CROP_WIDTH,
    PROP_CROP_HEIGHT,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_ifft_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_IFFT_TASK, NULL));
}

static void
ufo_ifft_task_setup (UfoTask *task,
                     UfoResources *resources,
                     GError **error)
{
    UfoIfftTaskPrivate *priv;

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    priv->pack_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_pack", NULL, error);
    priv->coeffs_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_compute_chirp_coeffs", NULL, error);
    priv->mul_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_multiply_chirp_coeffs", NULL, error);
    priv->c_mul_kernel = ufo_resources_get_kernel (resources, "complex.cl", "c_mul", NULL, error);
    priv->context = ufo_resources_get_context (resources);

    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainContext (priv->context), error);

    if (priv->pack_kernel != NULL)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->pack_kernel), error);

    if (priv->coeffs_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->coeffs_kernel), error);
    }

    if (priv->mul_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->mul_kernel), error);
    }

    if (priv->c_mul_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->c_mul_kernel), error);
    }

    if (priv->coeffs_buffer == NULL) {
        UfoRequisition requisition;
        requisition.n_dims = 2;
        requisition.dims[0] = 1;
        requisition.dims[1] = 1;

        priv->coeffs_buffer = ufo_buffer_new(&requisition, priv->context);
        priv->f_coeffs_buffer = ufo_buffer_new(&requisition, priv->context);
        priv->tmp_buffer = ufo_buffer_new(&requisition, priv->context);
        priv->tmp_buffer_2 = ufo_buffer_new(&requisition, priv->context);
    }
}

static void
ufo_ifft_task_get_requisition (UfoTask *task,
                               UfoBuffer **inputs,
                               UfoRequisition *requisition,
                               GError **error)
{
    UfoIfftTaskPrivate *priv;
    UfoRequisition in_req;
    cl_command_queue queue;

    if (ufo_buffer_get_layout (inputs[0]) != UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED) {
        g_set_error_literal (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                             "ifft input must be complex");
        return;
    }

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &in_req);

    priv->param.batch = 1;

    for (int i = 0; i < in_req.n_dims; i++) {
        if (priv->user_size[i] != 0 && priv->user_size[i] > in_req.dims[i]) {
            g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                         "Cropped size must be less than or equal to input size");
            return;
        }
        /* First the actual desired size */
        priv->fft_work_size[i] = in_req.dims[i];
        /* Now the next power of two (if the desired size is not a power of 2 ->
         * chirp-z -> next power of two of twice the size). Also do not pad if
         * the dimension is a batching one. */
        if (i <= priv->param.dimensions - 1) {
            if (priv->fft_work_size[i] != 2 * ufo_math_compute_closest_smaller_power_of_2 (priv->fft_work_size[i] - 1)) {
                priv->fft_work_size[i] = 2 * ufo_math_compute_closest_smaller_power_of_2 (2 * priv->fft_work_size[i] - 1);
            }
        }
    }
    /* Input requisition is 2 * width because of complex values */
    priv->fft_work_size[0] >>= 1;

    switch (priv->param.dimensions) {
        case UFO_FFT_3D:
            priv->param.size[2] = priv->fft_work_size[2];
        case UFO_FFT_2D:
            priv->param.size[1] = priv->fft_work_size[1];
        case UFO_FFT_1D:
            priv->param.size[0] = priv->fft_work_size[0];
    }

    switch (priv->param.dimensions) {
        case UFO_FFT_1D:
            priv->param.batch *= in_req.n_dims >= 2 ? in_req.dims[1] : 1;
        case UFO_FFT_2D:
            priv->param.batch *= in_req.n_dims == 3 ? in_req.dims[2] : 1;
        default:
            break;
    }

    queue = ufo_gpu_node_get_cmd_queue (UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task))));
    UFO_RESOURCES_CHECK_SET_AND_RETURN (ufo_fft_update (priv->fft, priv->context, queue, &priv->param), error);

    requisition->n_dims = in_req.n_dims;
    requisition->dims[0] = priv->user_size[0] == 0 ? in_req.dims[0] >> 1 : priv->user_size[0];
    requisition->dims[1] = priv->user_size[1] == 0 ? (in_req.n_dims >= 2 ? in_req.dims[1] : 1) : priv->user_size[1];
    requisition->dims[2] = priv->user_size[2] == 0 ? (in_req.n_dims == 3 ? in_req.dims[2] : 1) : priv->user_size[2];
}

static guint
ufo_ifft_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_ifft_task_get_num_dimensions (UfoTask *task,
                                  guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return UFO_IFFT_TASK_GET_PRIVATE (task)->param.dimensions > 2 ? 3 : 2;
}

static UfoTaskMode
ufo_ifft_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_ifft_task_equal_real (UfoNode *n1,
                          UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_IFFT_TASK (n1) && UFO_IS_IFFT_TASK (n2), FALSE);

    return UFO_IFFT_TASK (n1)->priv->pack_kernel == UFO_IFFT_TASK (n2)->priv->pack_kernel;
}

static gboolean
ufo_ifft_task_process (UfoTask *task,
                       UfoBuffer **inputs,
                       UfoBuffer *output,
                       UfoRequisition *requisition)
{
    UfoIfftTaskPrivate *priv;
    UfoRequisition in_req, fft_req;
    UfoProfiler *profiler;
    cl_command_queue queue;
    cl_mem in_mem, out_mem, tmp_mem, tmp_mem_2;
    cl_int out_width, out_height, false_value = 0;
    gsize in_work_size[3];
    gfloat scale = 1.0f;
    guint num_processed;
    gboolean do_chirp = FALSE;

    priv = UFO_IFFT_TASK_GET_PRIVATE (task);
    g_object_get (task, "num_processed", &num_processed, NULL);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    queue = ufo_gpu_node_get_cmd_queue (UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task))));
    in_mem = ufo_buffer_get_device_array (inputs[0], queue);
    out_mem = ufo_buffer_get_device_array (output, queue);

    ufo_buffer_get_requisition (inputs[0], &in_req);
    ufo_buffer_set_layout (output, UFO_BUFFER_LAYOUT_REAL);

    fft_req.n_dims = requisition->n_dims;
    fft_req.dims[0] = priv->fft_work_size[0] << 1;
    fft_req.dims[1] = priv->fft_work_size[1];
    fft_req.dims[2] = priv->fft_work_size[2];

    in_work_size[0] = in_req.dims[0] >> 1;
    in_work_size[1] = in_req.n_dims >= 2 ? in_req.dims[1] : 1;
    in_work_size[2] = in_req.n_dims == 3 ? in_req.dims[2] : 1;

    out_width  = (cl_int) requisition->dims[0];
    out_height = (cl_int) requisition->dims[1];

    /* Figure out if we need to do Chirp-z */
    for (int i = 0; i < requisition->n_dims; i++) {
        if (fft_req.dims[i] != in_req.dims[i]) {
            /* If FFT output (i.e. our input) is not a power of 2, we need Chirp-z */
            do_chirp = TRUE;
            break;
        }
    }

    if (do_chirp) {
        if (ufo_buffer_cmp_dimensions (priv->tmp_buffer, &fft_req) != 0) {
            ufo_buffer_resize (priv->tmp_buffer, &fft_req);
            ufo_buffer_resize (priv->tmp_buffer_2, &fft_req);
        }
        tmp_mem = ufo_buffer_get_device_array (priv->tmp_buffer, queue);
        tmp_mem_2 = ufo_buffer_get_device_array (priv->tmp_buffer_2, queue);
        ufo_fft_chirp_z (
            priv->fft,
            &priv->param,
            queue,
            profiler,
            in_mem,
            tmp_mem,
            tmp_mem_2,
            out_mem,
            priv->coeffs_buffer,
            priv->f_coeffs_buffer,
            priv->coeffs_kernel,
            priv->mul_kernel,
            priv->c_mul_kernel,
            priv->pack_kernel,
            in_work_size,
            priv->fft_work_size,
            /* FT work size is the input size here */
            in_work_size,
            requisition->n_dims,
            out_width,
            out_height,
            UFO_FFT_BACKWARD
        );
    } else {
        /* No Chirp-z needed -> do one pass and finish (classic FFT) */
        tmp_mem = in_mem;
        UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (priv->fft, queue, profiler,
                                                    tmp_mem, tmp_mem,
                                                    UFO_FFT_BACKWARD,
                                                    0, NULL, NULL));

        /* Crop and scale by the padded FFT size as well (on top ofscaling by input size in Chirp-z) */
        switch (priv->param.dimensions) {
            case UFO_FFT_3D:
                scale /= (gfloat) priv->param.size[2];
            case UFO_FFT_2D:
                scale /= (gfloat) priv->param.size[1];
            case UFO_FFT_1D:
                scale /= (gfloat) priv->param.size[0];
        }
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 0, sizeof (cl_mem), (gpointer) &tmp_mem));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 1, sizeof (cl_mem), (gpointer) &out_mem));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 2, sizeof (cl_int), &out_width));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 3, sizeof (cl_int), &out_height));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 4, sizeof (gfloat), &scale));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack_kernel, 5, sizeof (cl_int), &false_value));
        ufo_profiler_call (profiler, queue, priv->pack_kernel, fft_req.n_dims, priv->fft_work_size, NULL);
    }

    if (!num_processed) {
        g_log ("fft",
            G_LOG_LEVEL_DEBUG,
            "IFFT work sizes: input=(w=%lu, h=%lu, d=%lu, ND=%u), intermediate=(w=%lu, h=%lu, d=%lu, ND=%u), "
            "output=(w=%lu, h=%lu, d=%lu, ND=%u), parameter=(w=%lu h=%lu d=%lu ND=%d batches=%lu), do_chirp=%d",
            in_work_size[0], in_work_size[1], in_work_size[2], in_req.n_dims,
            priv->fft_work_size[0], priv->fft_work_size[1], priv->fft_work_size[2], fft_req.n_dims,
            requisition->dims[0], requisition->dims[1], requisition->dims[2], requisition->n_dims,
            priv->param.size[0], priv->param.size[1], priv->param.size[2], priv->param.dimensions, priv->param.batch,
            do_chirp
        );
    }

    return TRUE;
}

static void
ufo_ifft_task_finalize (GObject *object)
{
    UfoIfftTaskPrivate *priv;

    priv = UFO_IFFT_TASK_GET_PRIVATE (object);

    if (priv->pack_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->pack_kernel));
        priv->pack_kernel = NULL;
    }

    if (priv->coeffs_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->coeffs_kernel));
        priv->coeffs_kernel = NULL;
    }

    if (priv->mul_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->mul_kernel));
        priv->mul_kernel = NULL;
    }

    if (priv->c_mul_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->c_mul_kernel));
        priv->c_mul_kernel = NULL;
    }

    if (priv->coeffs_buffer) {
        g_object_unref(priv->coeffs_buffer);
        priv->coeffs_buffer = NULL;
    }

    if (priv->f_coeffs_buffer) {
        g_object_unref(priv->f_coeffs_buffer);
        priv->f_coeffs_buffer = NULL;
    }

    if (priv->tmp_buffer) {
        g_object_unref(priv->tmp_buffer);
        priv->tmp_buffer = NULL;
    }

    if (priv->tmp_buffer_2) {
        g_object_unref(priv->tmp_buffer_2);
        priv->tmp_buffer_2 = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    if (priv->fft) {
        ufo_fft_destroy (priv->fft);
        priv->fft = NULL;
    }

    G_OBJECT_CLASS (ufo_ifft_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_ifft_task_setup;
    iface->get_requisition = ufo_ifft_task_get_requisition;
    iface->get_num_inputs = ufo_ifft_task_get_num_inputs;
    iface->get_num_dimensions = ufo_ifft_task_get_num_dimensions;
    iface->get_mode = ufo_ifft_task_get_mode;
    iface->process = ufo_ifft_task_process;
}

static void
ufo_ifft_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoIfftTaskPrivate *priv = UFO_IFFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_DIMENSIONS:
            priv->param.dimensions = g_value_get_uint (value);
            break;
        case PROP_CROP_WIDTH:
            priv->user_size[0] = g_value_get_uint (value);
            break;
        case PROP_CROP_HEIGHT:
            priv->user_size[1] = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_ifft_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoIfftTaskPrivate *priv = UFO_IFFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_DIMENSIONS:
            g_value_set_uint (value, priv->param.dimensions);
            break;
        case PROP_CROP_WIDTH:
            g_value_set_uint (value, priv->user_size[0]);
            break;
        case PROP_CROP_HEIGHT:
            g_value_set_uint (value, priv->user_size[1]);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_ifft_task_class_init (UfoIfftTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;

    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->finalize = ufo_ifft_task_finalize;
    oclass->set_property = ufo_ifft_task_set_property;
    oclass->get_property = ufo_ifft_task_get_property;

    properties[PROP_DIMENSIONS] =
        g_param_spec_uint ("dimensions",
            "Number of IFFT dimensions from 1 to 3",
            "Number of IFFT dimensions from 1 to 3",
            1, 3, 1,
            G_PARAM_READWRITE);

    properties[PROP_CROP_WIDTH] =
        g_param_spec_uint ("crop-width",
            "Width of cropped output",
            "Width of cropped output",
            0, 32768, 0,
            G_PARAM_READWRITE);

    properties[PROP_CROP_HEIGHT] =
        g_param_spec_uint ("crop-height",
            "Height of cropped output",
            "Height of cropped output",
            0, 32768, 0,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->equal = ufo_ifft_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoIfftTaskPrivate));
}

static void
ufo_ifft_task_init (UfoIfftTask *self)
{
    UfoIfftTaskPrivate *priv;
    self->priv = priv = UFO_IFFT_TASK_GET_PRIVATE (self);
    priv->pack_kernel = NULL;
    priv->coeffs_kernel = NULL;
    priv->mul_kernel = NULL;
    priv->c_mul_kernel = NULL;
    priv->coeffs_buffer = NULL;
    priv->f_coeffs_buffer = NULL;
    priv->tmp_buffer = NULL;
    priv->tmp_buffer_2 = NULL;
    priv->context = NULL;
    priv->fft = ufo_fft_new ();
    priv->param.dimensions = UFO_FFT_1D;
    for (int i = 0; i < 3; i++) {
        /* Size of the FFT plan */
        priv->param.size[i] = 1;
        /* Intermediate size for computing either full FFTs or batches */
        priv->fft_work_size[i] = 1;
        /* Final size possibly non-power-of-two, 0 = not specified*/
        priv->user_size[i] = 0;
    }
    priv->param.batch = 1;
}
