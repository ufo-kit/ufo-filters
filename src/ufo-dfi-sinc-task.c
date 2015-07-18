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

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ufo-dfi-sinc-task.h"

#define M_PI 3.14159265358979323846
#define BLOCK_SIZE 16

/**
 * SECTION:ufo-dfi-sinc-task
 * @Short_description: Compute the 2D Fourier spectrum of reconstructed image
 * using 1D Fourier projection of sinogram and sinc interpolation
 * @Title: dfi-sinc
 *
 * Computes the 2D Fourier spectrum of reconstructed image using
 * 1D Fourier prijection of sinogram (fft filter should be applied before).
 * There are no default values for properties, therefore they should be
 * assign manually. The property #UfoDfiSincTask:kernel-size is the length
 * of kernel which will be used in interpolation, #UfoDfiSincTask:number-presampled-values
 * is the number of presampled values which will be used to calculate
 * #UfoDfiSincTask:kernel-size kernel coefficients, #UfoDfiSincTask:roi-size - is the
 * length of one side of Region of Interest.
 *
 */

static void ufo_task_interface_init (UfoTaskIface *iface);
G_DEFINE_TYPE_WITH_CODE (UfoDfiSincTask, ufo_dfi_sinc_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_DFI_SINC_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_DFI_SINC_TASK, UfoDfiSincTaskPrivate))

struct _UfoDfiSincTaskPrivate {
    UfoResources     *resources;
    cl_context       context;
    cl_command_queue cmd_queue;

    cl_kernel        dfi_sinc_kernel;
    cl_kernel        clear_kernel;

    UfoRequisition  last_input_req;
    UfoBuffer       *ktbl_buffer;
    cl_mem          in_tex;

    guint L;
    guint number_presampled_values;
    gint  roi_size;

    cl_int  interp_grid_cols;
    DfiSincData dfi_data;
};

enum {
    PROP_0,
    PROP_L,
    PROP_NUM_PRESAMPLED_VLS,
    PROP_ROI_SIZE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_dfi_sinc_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_DFI_SINC_TASK, NULL));
}

/**
 * ufo_dfi_sinc_task_hammingw:
 * @i:  the number of current element in the array
 * @length:  the length of array
 *
 * http://en.wikipedia.org/wiki/Hamming_window#Hamming_window
 *
 * Returns: a float.
 */
static gfloat
ufo_dfi_sinc_task_hammingw(int i, int length)
{
    return (gfloat)(0.54f - 0.46f * cos (2 * M_PI * ((gfloat) i/ (gfloat) length)));
}

/**
 * ufo_dfi_sinc_task_sinc:
 * @i:  the current number of sample of sinc function
 *
 * Returns: a float.
 */
static gfloat
ufo_dfi_sinc_task_sinc(gfloat x)
{
    return (x == 0.0f) ? 1.0f : (gfloat)(sin (M_PI * x) / (M_PI * x));
}

/**
 * ufo_dfi_sinc_task_get_ktbl:
 * @length:  the length of array of presampled kernel values
 *
 * Returns: an array.
 */
static gfloat *
ufo_dfi_sinc_task_get_ktbl(size_t length)
{
    gfloat *ktbl = (gfloat *) g_malloc0 (length * sizeof (gfloat));

    if (!length % 2) {
        g_print("Error: Length %zu of ktbl cannot be even!\n", length);
        exit(1);
    }

    size_t ktbl_len2 = (length - 1)/2;
    gfloat step = (gfloat) M_PI / (gfloat) ktbl_len2;
    gfloat value = -(gfloat)ktbl_len2 * step;

    for (size_t i = 0; i < length; ++i, value += step) {
        ktbl[i] = ufo_dfi_sinc_task_sinc ((gfloat) value) * ufo_dfi_sinc_task_hammingw ((gint) i, (gint) length);
    }

    return ktbl;
}

static void
ufo_dfi_sinc_task_setup (UfoTask *task,
                         UfoResources *resources,
                         GError **error)
{
    UfoDfiSincTaskPrivate *priv = UFO_DFI_SINC_TASK_GET_PRIVATE (task);
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));

    priv->resources = g_object_ref (resources);
    priv->context   = ufo_resources_get_context (resources);
    priv->cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    UFO_RESOURCES_CHECK_CLERR (clRetainCommandQueue (priv->cmd_queue));

    //create kernel
    priv->dfi_sinc_kernel = ufo_resources_get_kernel (resources, "dfi.cl", "dfi_sinc_kernel", error);
    priv->clear_kernel = ufo_resources_get_kernel (resources, "dfi.cl", "clear_kernel", error);

    //calculate and setup kernel lookup table to buffer
    UfoRequisition ktbl_requisition;
    ktbl_requisition.n_dims = 2;
    ktbl_requisition.dims[0] = priv->number_presampled_values;
    ktbl_requisition.dims[1] = 1;

    priv->ktbl_buffer = ufo_buffer_new (&ktbl_requisition, priv->context);
    gfloat *h_ktbl_buffer = ufo_buffer_get_host_array (priv->ktbl_buffer, priv->cmd_queue);

    gfloat *tmp_ktbl = ufo_dfi_sinc_task_get_ktbl (priv->number_presampled_values);
    memcpy((void *)h_ktbl_buffer, (const void *)tmp_ktbl, priv->number_presampled_values * sizeof (gfloat));
    g_free (tmp_ktbl);

    // calculate some common variables
    priv->dfi_data.half_kernel_length = priv->L * 0.5f;
    priv->dfi_data.half_ktbl_length   = (priv->number_presampled_values - 1) * 0.5f;
    priv->dfi_data.table_spacing      = ((cl_float) priv->number_presampled_values) / priv->L;
}

static void
ufo_dfi_sinc_task_get_requisition (UfoTask *task,
                                   UfoBuffer **inputs,
                                   UfoRequisition *requisition)
{
    //set output requsition
    UfoRequisition input_requisition;
    ufo_buffer_get_requisition (inputs[0], &input_requisition);
    requisition->n_dims = 2;
    requisition->dims[0] = input_requisition.dims[0];
    requisition->dims[1] = input_requisition.dims[0] / 2;
}

static guint
ufo_dfi_sinc_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_dfi_sinc_task_get_num_dimensions (UfoTask *task,
                                      guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_dfi_sinc_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_dfi_sinc_task_process (UfoTask *task,
                           UfoBuffer **inputs,
                           UfoBuffer *output,
                           UfoRequisition *requisition)
{
    UfoDfiSincTaskPrivate *priv = UFO_DFI_SINC_TASK_GET_PRIVATE (task);

    UfoRequisition input_requisition;
    ufo_buffer_get_requisition (inputs[0], &input_requisition);

    gboolean size_changed = input_requisition.n_dims != priv->last_input_req.n_dims;

    for (guint i = 0; !size_changed && i < input_requisition.n_dims; i++) {
        size_changed = input_requisition.dims[i] != priv->last_input_req.dims[i];
    }

    if (size_changed) {
        priv->dfi_data.raster_size        = (cl_int) (input_requisition.dims[0] * 0.5f);
        priv->dfi_data.inv_angle_step_rad = (cl_float) input_requisition.dims[1] / M_PI;
        priv->dfi_data.theta_max          = (cl_float) input_requisition.dims[1];
        priv->dfi_data.rho_max            = (cl_float) input_requisition.dims[0] * 0.5f;

        if (priv->roi_size >= 1 && priv->roi_size <= priv->dfi_data.raster_size) {
            priv->interp_grid_cols = (cl_int) ceil ((gfloat) priv->roi_size / (gfloat) BLOCK_SIZE);
        }
        else {
            priv->interp_grid_cols = (cl_int) ceil((gfloat)priv->dfi_data.raster_size / (gfloat)BLOCK_SIZE);
        }

        priv->dfi_data.spectrum_offset = (priv->dfi_data.raster_size - priv->interp_grid_cols * BLOCK_SIZE) * 0.5f;
        priv->dfi_data.radius_max      = (cl_float) (priv->interp_grid_cols * BLOCK_SIZE) * 0.5f;

        // Setup texture
        // It should be recreated if input reqs was changed

        if (priv->in_tex) {
            UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->in_tex));
            priv->in_tex = NULL;
        }

        cl_image_format format;
        format.image_channel_order = CL_RG;
        format.image_channel_data_type = CL_FLOAT;

        cl_mem_flags flags = CL_MEM_READ_WRITE;
        gsize width  = input_requisition.dims[0]/2;
        gsize height = input_requisition.dims[1];

        cl_int eer2;
        priv->in_tex = clCreateImage2D (priv->context,
                                        flags, &format,
                                        width, height, 0,
                                        NULL, &eer2);

        UFO_RESOURCES_CHECK_CLERR (eer2);
    }

    gpointer cmd_queue = priv->cmd_queue;
    gpointer in_tex = priv->in_tex;

    static size_t local_work_size[2] = {BLOCK_SIZE, BLOCK_SIZE};
    size_t empty_kernel_working_size[2] = {(size_t)priv->dfi_data.raster_size,
                                           (size_t)priv->dfi_data.raster_size};

    cl_mem input_mem = ufo_buffer_get_device_array (inputs[0], priv->cmd_queue);
    cl_mem out_mem   = ufo_buffer_get_device_array (output, priv->cmd_queue);
    cl_mem ktbl_mem  = ufo_buffer_get_device_image (priv->ktbl_buffer, priv->cmd_queue);

    UfoProfiler *profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    // clear output
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->clear_kernel, 0, sizeof (cl_mem), &out_mem));
    ufo_profiler_call (profiler, cmd_queue, priv->clear_kernel,
                       requisition->n_dims, empty_kernel_working_size, local_work_size);

    // copy input to the special texture
    static size_t zero_offset[3] = {0, 0, 0};
    size_t projection_region[3]  = {(size_t) (input_requisition.dims[0] * 0.5f),
                                    input_requisition.dims[1],
                                    1};

    UFO_RESOURCES_CHECK_CLERR (clEnqueueCopyBufferToImage (cmd_queue,
                                                           input_mem,
                                                           in_tex,
                                                           0,
                                                           zero_offset,
                                                           projection_region,
                                                           0, NULL, NULL));

    // execute DFI kernel
    size_t working_size[2] = {(size_t)(priv->interp_grid_cols * BLOCK_SIZE),
                              (size_t)(priv->interp_grid_cols * BLOCK_SIZE)};

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->dfi_sinc_kernel, 0, sizeof (cl_mem), &priv->in_tex));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->dfi_sinc_kernel, 1, sizeof (cl_mem), &ktbl_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->dfi_sinc_kernel, 2, sizeof (DfiSincData), &priv->dfi_data));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->dfi_sinc_kernel, 3, sizeof (cl_mem), &out_mem));

    ufo_profiler_call (profiler, priv->cmd_queue, priv->dfi_sinc_kernel,
                       requisition->n_dims, working_size, local_work_size);
    return TRUE;
}

static void
ufo_dfi_sinc_task_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
    UfoDfiSincTaskPrivate *priv = UFO_DFI_SINC_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_L:
            priv->L = g_value_get_uint (value);
            break;
        case PROP_NUM_PRESAMPLED_VLS:
            priv->number_presampled_values = g_value_get_uint (value);
            break;
        case PROP_ROI_SIZE:
            priv->roi_size = g_value_get_int (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_dfi_sinc_task_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
    UfoDfiSincTaskPrivate *priv = UFO_DFI_SINC_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_L:
            g_value_set_uint (value, priv->L);
            break;
        case PROP_NUM_PRESAMPLED_VLS:
            g_value_set_uint (value, priv->number_presampled_values);
            break;
        case PROP_ROI_SIZE:
            g_value_set_int (value, priv->roi_size);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_dfi_sinc_task_finalize (GObject *object)
{
    UfoDfiSincTaskPrivate *priv = UFO_DFI_SINC_TASK_GET_PRIVATE (object);
    G_OBJECT_CLASS (ufo_dfi_sinc_task_parent_class)->finalize (object);
}

static void
ufo_dfi_sinc_task_dispose (GObject *object)
{
    UfoDfiSincTaskPrivate *priv = UFO_DFI_SINC_TASK_GET_PRIVATE (object);

    if (priv->resources != NULL) {
        g_object_unref (priv->resources);
        priv->resources = NULL;
    }

    if (priv->ktbl_buffer != NULL) {
        g_object_unref (priv->ktbl_buffer);
        priv->ktbl_buffer = NULL;
    }

    G_OBJECT_CLASS (ufo_dfi_sinc_task_parent_class)->dispose (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_dfi_sinc_task_setup;
    iface->get_num_inputs = ufo_dfi_sinc_task_get_num_inputs;
    iface->get_num_dimensions = ufo_dfi_sinc_task_get_num_dimensions;
    iface->get_mode = ufo_dfi_sinc_task_get_mode;
    iface->get_requisition = ufo_dfi_sinc_task_get_requisition;
    iface->process = ufo_dfi_sinc_task_process;
}

static void
ufo_dfi_sinc_task_class_init (UfoDfiSincTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_dfi_sinc_task_set_property;
    gobject_class->get_property = ufo_dfi_sinc_task_get_property;
    gobject_class->finalize = ufo_dfi_sinc_task_finalize;
    gobject_class->dispose = ufo_dfi_sinc_task_dispose;

    properties[PROP_L] =
        g_param_spec_uint ("kernel-size",
            "Kernel size",
            "The length of kernel which will be used in interpolation.",
            1, 25, 7,
            G_PARAM_WRITABLE);

    properties[PROP_NUM_PRESAMPLED_VLS] =
        g_param_spec_uint ("number-presampled-values",
            "Number of presampled values",
            "Number of presampled values which will be used to calculate L kernel coefficients.",
            1, 16383, 2047,
            G_PARAM_WRITABLE);

    properties[PROP_ROI_SIZE] =
        g_param_spec_int ("roi-size",
            "Size of Region of Interest",
            "The length of one side of Region of Interest.",
            -1, G_MAXINT, -1,
            G_PARAM_WRITABLE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoDfiSincTaskPrivate));
}

static void
ufo_dfi_sinc_task_init(UfoDfiSincTask *self)
{
    UfoDfiSincTaskPrivate *priv = NULL;
    self->priv = priv = UFO_DFI_SINC_TASK_GET_PRIVATE(self);
    priv->resources = NULL;
    priv->dfi_sinc_kernel = NULL;
    priv->in_tex = NULL;

    priv->L = 7;
    priv->number_presampled_values = 2047;
    priv->roi_size = 0;

    priv->last_input_req.n_dims = 0;
}
