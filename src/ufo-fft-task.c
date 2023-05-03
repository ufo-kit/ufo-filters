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

#include <string.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-fft-task.h"
#include "common/ufo-math.h"
#include "common/ufo-fft.h"


struct _UfoFftTaskPrivate {
    UfoFft *fft;
    UfoFftParameter param;

    cl_context context;
    cl_kernel spread_kernel, pack_kernel, coeffs_kernel, mul_kernel, c_mul_kernel;

    gboolean zeropad;

    UfoBuffer *coeffs_buffer, *f_coeffs_buffer, *tmp_buffer;
    gsize user_size[3], fft_work_size[3];
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFftTask, ufo_fft_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_FFT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FFT_TASK, UfoFftTaskPrivate))

enum {
    PROP_0,
    PROP_ZEROPADDING,
    PROP_DIMENSIONS,
    PROP_SIZE_X,
    PROP_SIZE_Y,
    PROP_SIZE_Z,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_fft_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FFT_TASK, NULL));
}

static void
ufo_fft_task_setup (UfoTask *task,
                    UfoResources *resources,
                    GError **error)
{
    UfoFftTaskPrivate *priv;

    priv = UFO_FFT_TASK_GET_PRIVATE (task);

    priv->spread_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_spread", NULL, error);
    priv->pack_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_pack", NULL, error);
    priv->coeffs_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_compute_chirp_coeffs", NULL, error);
    priv->mul_kernel = ufo_resources_get_kernel (resources, "fft.cl", "fft_multiply_chirp_coeffs", NULL, error);
    priv->c_mul_kernel = ufo_resources_get_kernel (resources, "complex.cl", "c_mul", NULL, error);

    priv->context = ufo_resources_get_context (resources);
    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainContext (priv->context), error);

    if (priv->spread_kernel != NULL)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->spread_kernel), error);

    if (priv->pack_kernel != NULL) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->pack_kernel), error);
    }

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
    }
}

static void
ufo_fft_task_get_requisition (UfoTask *task,
                              UfoBuffer **inputs,
                              UfoRequisition *requisition,
                              GError **error)
{
    UfoFftTaskPrivate *priv;
    UfoRequisition in_req;
    cl_command_queue queue;

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &in_req);

    priv->param.batch = 1;

    for (int i = 0; i < in_req.n_dims; i++) {
        if (priv->user_size[i] != 0 && priv->user_size[i] < in_req.dims[i]) {
            g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                         "Specified size must be greater than or equal to input size");
            return;
        }
        /* First the actual desired size */
        if (priv->user_size[i] == 0) {
            priv->fft_work_size[i] = in_req.dims[i];
            if (priv->zeropad && i <= priv->param.dimensions - 1) {
                /* History */
                priv->fft_work_size[i] = 2 * ufo_math_compute_closest_smaller_power_of_2 (priv->fft_work_size[i] - 1);
            }
        } else {
            priv->fft_work_size[i] = priv->user_size[i];
        }
        /* Up to this point FFT size and output size are the same */
        requisition->dims[i] = i <= in_req.n_dims - 1 ? priv->fft_work_size[i] : 1;
        /* Now the next power of two (if the desired size is not a power of 2 ->
         * chirp-z -> next power of two of twice the size). Also do not pad if
         * the dimension is a batching one. */
        if (i <= priv->param.dimensions - 1) {
            if (priv->fft_work_size[i] != 2 * ufo_math_compute_closest_smaller_power_of_2 (priv->fft_work_size[i] - 1)) {
                priv->fft_work_size[i] = 2 * ufo_math_compute_closest_smaller_power_of_2 (2 * priv->fft_work_size[i] - 1);
            }
        }
    }

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
    /* Complex interleaved */
    requisition->dims[0] <<= 1;
}

static guint
ufo_fft_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_fft_task_get_num_dimensions (UfoTask *task,
                                 guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return UFO_FFT_TASK_GET_PRIVATE (task)->param.dimensions > 2 ? 3 : 2;
}

static UfoTaskMode
ufo_fft_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_fft_task_equal_real (UfoNode *n1,
                            UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_FFT_TASK (n1) && UFO_IS_FFT_TASK (n2), FALSE);
    return UFO_FFT_TASK (n1)->priv->spread_kernel == UFO_FFT_TASK (n2)->priv->spread_kernel;
}

static gboolean
ufo_fft_task_process (UfoTask *task,
                      UfoBuffer **inputs,
                      UfoBuffer *output,
                      UfoRequisition *requisition)
{
    UfoFftTaskPrivate *priv;
    UfoRequisition in_req, fft_req;
    UfoProfiler *profiler;
    cl_command_queue queue;
    cl_mem in_mem, out_mem, tmp_mem;
    cl_int in_width, in_height, in_depth;
    gsize in_work_size[3], ft_work_size[3];
    guint num_processed;
    gboolean do_chirp = FALSE;

    priv = UFO_FFT_TASK_GET_PRIVATE (task);
    g_object_get (task, "num_processed", &num_processed, NULL);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    queue = ufo_gpu_node_get_cmd_queue (UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task))));
    in_mem = ufo_buffer_get_device_array (inputs[0], queue);
    out_mem = ufo_buffer_get_device_array (output, queue);

    ufo_buffer_get_requisition (inputs[0], &in_req);
    ufo_buffer_set_layout (output, UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED);

    fft_req.n_dims = requisition->n_dims;
    fft_req.dims[0] = priv->fft_work_size[0] << 1;
    fft_req.dims[1] = priv->fft_work_size[1];
    fft_req.dims[2] = priv->fft_work_size[2];

    in_work_size[0] = in_req.dims[0];
    in_work_size[1] = in_req.n_dims >= 2 ? in_req.dims[1] : 1;
    in_work_size[2] = in_req.n_dims == 3 ? in_req.dims[2] : 1;

    in_width  = (cl_int) in_work_size[0];
    in_height = (cl_int) in_work_size[1];
    in_depth  = (cl_int) in_work_size[2];
    ft_work_size[0] = requisition->dims[0] >> 1;
    ft_work_size[1] = requisition->dims[1];
    ft_work_size[2] = requisition->dims[2];

    /* Figure out if we need to do Chirp-z */
    for (int i = 0; i < requisition->n_dims; i++) {
        if (fft_req.dims[i] != requisition->dims[i]) {
            /* If desired size is not power of 2 we need Chirp-z */
            do_chirp = TRUE;
            break;
        }
    }

    if (do_chirp) {
        if (ufo_buffer_cmp_dimensions (priv->tmp_buffer, &fft_req) != 0) {
            ufo_buffer_resize (priv->tmp_buffer, &fft_req);
        }
        tmp_mem = ufo_buffer_get_device_array (priv->tmp_buffer, queue);
    } else {
        /* Just one forward pass in the out_mem, which already has the correct size */
        tmp_mem = out_mem;
    }

    /* Pad to the power of two, that happens always, no matter the size of the output */
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread_kernel, 0, sizeof (cl_mem), (gpointer) &tmp_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread_kernel, 1, sizeof (cl_mem), (gpointer) &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread_kernel, 2, sizeof (cl_int), &in_width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread_kernel, 3, sizeof (cl_int), &in_height));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread_kernel, 4, sizeof (cl_int), &in_depth));
    ufo_profiler_call (profiler, queue, priv->spread_kernel, 3, priv->fft_work_size, NULL);

    if (do_chirp) {
        ufo_fft_chirp_z (
            priv->fft,
            &priv->param,
            queue,
            profiler,
            in_mem,
            tmp_mem,
            out_mem,
            priv->coeffs_buffer,
            priv->f_coeffs_buffer,
            priv->coeffs_kernel,
            priv->mul_kernel,
            priv->c_mul_kernel,
            priv->pack_kernel,
            in_work_size,
            priv->fft_work_size,
            ft_work_size,
            requisition->n_dims,
            (cl_int) ft_work_size[0],
            (cl_int) ft_work_size[1],
            UFO_FFT_FORWARD
        );
    } else {
        /* No Chirp-z needed -> do one pass and finish (classic FFT) */
        UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (priv->fft, queue, profiler,
                                                    tmp_mem, tmp_mem,
                                                    UFO_FFT_FORWARD,
                                                    0, NULL, NULL));
    }

    if (!num_processed) {
        g_log ("fft",
            G_LOG_LEVEL_DEBUG,
            "FFT work sizes: input=(w=%lu, h=%lu, d=%lu, ND=%u), intermediate=(w=%lu, h=%lu, d=%lu, ND=%u), "
            "output=(w=%lu, h=%lu, d=%lu, ND=%u), parameter=(w=%lu h=%lu d=%lu ND=%d batches=%lu), do_chirp=%d",
            in_work_size[0], in_work_size[1], in_work_size[2], in_req.n_dims,
            priv->fft_work_size[0], priv->fft_work_size[1], priv->fft_work_size[2], fft_req.n_dims,
            requisition->dims[0] / 2, requisition->dims[1], requisition->dims[2], requisition->n_dims,
            priv->param.size[0], priv->param.size[1], priv->param.size[2], priv->param.dimensions, priv->param.batch,
            do_chirp
        );
    }

    return TRUE;
}

static void
ufo_fft_task_finalize (GObject *object)
{
    UfoFftTaskPrivate *priv;

    priv = UFO_FFT_TASK_GET_PRIVATE (object);

    if (priv->spread_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->spread_kernel));
        priv->spread_kernel = NULL;
    }

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

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    if (priv->fft) {
        ufo_fft_destroy (priv->fft);
        priv->fft = NULL;
    }

    G_OBJECT_CLASS (ufo_fft_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_fft_task_setup;
    iface->get_requisition = ufo_fft_task_get_requisition;
    iface->get_num_inputs = ufo_fft_task_get_num_inputs;
    iface->get_num_dimensions = ufo_fft_task_get_num_dimensions;
    iface->get_mode = ufo_fft_task_get_mode;
    iface->process = ufo_fft_task_process;
}

static void
ufo_fft_task_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
    UfoFftTaskPrivate *priv = UFO_FFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_ZEROPADDING:
            priv->zeropad = g_value_get_boolean (value);
            break;
        case PROP_DIMENSIONS:
            priv->param.dimensions = g_value_get_uint (value);
            break;
        case PROP_SIZE_X:
            priv->user_size[0] = g_value_get_uint (value);
            break;
        case PROP_SIZE_Y:
            priv->user_size[1] = g_value_get_uint (value);
            break;
        case PROP_SIZE_Z:
            priv->user_size[2] = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_fft_task_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
    UfoFftTaskPrivate *priv = UFO_FFT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_ZEROPADDING:
            g_value_set_boolean (value, priv->zeropad);
            break;
        case PROP_DIMENSIONS:
            g_value_set_uint (value, priv->param.dimensions);
            break;
        case PROP_SIZE_X:
            g_value_set_uint (value, priv->user_size[0]);
            break;
        case PROP_SIZE_Y:
            g_value_set_uint (value, priv->user_size[1]);
            break;
        case PROP_SIZE_Z:
            g_value_set_uint (value, priv->user_size[2]);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_fft_task_class_init (UfoFftTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;

    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->finalize = ufo_fft_task_finalize;
    oclass->set_property = ufo_fft_task_set_property;
    oclass->get_property = ufo_fft_task_get_property;

    properties[PROP_ZEROPADDING] =
        g_param_spec_boolean("auto-zeropadding",
            "Auto zeropadding to next power of 2 value",
            "Auto zeropadding to next power of 2 value",
            TRUE,
            G_PARAM_READWRITE);

    properties[PROP_DIMENSIONS] =
        g_param_spec_uint("dimensions",
            "Number of FFT dimensions from 1 to 3",
            "Number of FFT dimensions from 1 to 3",
            1, 3, 1,
            G_PARAM_READWRITE);

    properties[PROP_SIZE_X] =
        g_param_spec_uint("size-x",
            "Size of the FFT transform in x-direction (zero-padded if larger than input)",
            "Size of the FFT transform in x-direction (zero-padded if larger than input)",
            0, 32768, 0,
            G_PARAM_READWRITE);

    properties[PROP_SIZE_Y] =
        g_param_spec_uint("size-y",
            "Size of the FFT transform in y-direction (zero-padded if larger than input)",
            "Size of the FFT transform in y-direction (zero-padded if larger than input)",
            0, 32768, 0,
            G_PARAM_READWRITE);

    properties[PROP_SIZE_Z] =
        g_param_spec_uint("size-z",
            "Size of the FFT transform in z-direction (zero-padded if larger than input)",
            "Size of the FFT transform in z-direction (zero-padded if larger than input)",
            0, 32768, 0,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->equal = ufo_fft_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoFftTaskPrivate));
}

static void
ufo_fft_task_init (UfoFftTask *self)
{
    UfoFftTaskPrivate *priv;

    self->priv = priv = UFO_FFT_TASK_GET_PRIVATE (self);

    priv->spread_kernel = NULL;
    priv->pack_kernel = NULL;
    priv->coeffs_kernel = NULL;
    priv->mul_kernel = NULL;
    priv->c_mul_kernel = NULL;
    priv->coeffs_buffer = NULL;
    priv->f_coeffs_buffer = NULL;
    priv->tmp_buffer = NULL;
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
    priv->param.zeropad = priv->zeropad = TRUE;
}
