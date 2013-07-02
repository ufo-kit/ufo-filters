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
#include <clFFT.h>

#include "ufo-dfi-sinc-task.h"

#define M_PI 3.14159265358979323846
#define BLOCK_SIZE 16

#define CL_CHECK_ERROR(FUNC) \
{ \
cl_int err = FUNC; \
if (err != CL_SUCCESS) { \
fprintf(stderr, "Error %d executing %s on %d!\n",\
err, __FILE__, __LINE__); \
abort(); \
}; \
}

/**
 * SECTION:ufo-dfi-sinc-task
 * @Short_description: Write TIFF files
 * @Title: dfi_sinc
 *
 */

struct _UfoDfiSincTaskPrivate {
    UfoResources *resources;
    cl_kernel dfi_sinc_kernel;
    cl_kernel clear_kernel;

    UfoBuffer *ktbl_buffer;

    guint number_presampled_values;
    guint L;
    gfloat oversampling;

    gint roi_size;
    cl_float L2;
    cl_int ktbl_len2;
    cl_int raster_size;
    cl_int raster_size2;
    cl_float table_spacing;
    cl_float angle_step_rad;
    cl_float theta_max;
    cl_float rho_max;

    gint spectrum_offset;
    gfloat max_radius;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoDfiSincTask, ufo_dfi_sinc_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_DFI_SINC_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_DFI_SINC_TASK, UfoDfiSincTaskPrivate))

enum {
    PROP_0,
    PROP_L,
    PROP_NUM_PRESAMPLED_VLS,
    PROP_OVERSAMPLING,
    PROP_ROI_SIZE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_dfi_sinc_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_DFI_SINC_TASK, NULL));
}

static gfloat
ufo_gridding_task_sinc(gfloat x) {
  return (x == 0.0f) ? 1.0 : sin(M_PI * x)/(M_PI * x);
} 

static gfloat *
ufo_gridding_task_get_ktbl(gint length) 
{
    gfloat *ktbl = (gfloat *)g_malloc0(length * sizeof (gfloat));

    if (!length%2) {
      g_print("Error: Length of ktbl cannot be even!\n");
      exit(1);
    }

    gint ktbl_len2 = (length - 1)/2;
    gfloat step = M_PI/(gfloat)ktbl_len2;

    gfloat value = -ktbl_len2 * step;

    for (int i = 0; i < length; ++i, value += step) {
      ktbl[i] = ufo_gridding_task_sinc(value) * (0.54f - 0.46f * cos(2*M_PI*((gfloat)i/(gfloat)length)));
    }

    return ktbl;
}

static void
ufo_dfi_sinc_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoDfiSincTaskPrivate *priv;
    cl_context context;
    cl_command_queue cmd_queue;

    priv = UFO_DFI_SINC_TASK_GET_PRIVATE (task);
    context = ufo_resources_get_context(resources);
    cmd_queue = g_list_nth_data(ufo_resources_get_cmd_queues(resources), 0);

    //obtain resources
    priv->resources = resources;

    //create kernel
    priv->dfi_sinc_kernel = ufo_resources_get_kernel(resources, "dfi_sinc_kernel.cl", "dfi_sinc_kernel", error);
    priv->clear_kernel = ufo_resources_get_kernel(resources, "dfi_sinc_kernel.cl", "clear_kernel", error);

    //calculate and setup kernel lookup table to buffer
    gfloat *tmp_ktbl = ufo_gridding_task_get_ktbl(priv->number_presampled_values);
    UfoRequisition ktbl_requisition;
    ktbl_requisition.n_dims = 2;
    ktbl_requisition.dims[0] = priv->number_presampled_values;
    ktbl_requisition.dims[1] = 1;

    priv->ktbl_buffer = ufo_buffer_new(&ktbl_requisition, context);
    gfloat *h_ktbl_buffer = ufo_buffer_get_host_array(priv->ktbl_buffer, cmd_queue);
    memcpy((void *)h_ktbl_buffer, (const void *)tmp_ktbl, priv->number_presampled_values * sizeof (gfloat));
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
    requisition->dims[1] = input_requisition.dims[0]/2;

    g_print("ufo_dfi_sinc_task_get_requisition (complex): 2 * input_requisition.dims[0] = 2 * %d\n", (int)(input_requisition.dims[0]/2));
    g_print("ufo_dfi_sinc_task_get_requisition (number): input_requisition.dims[1] = %d\n", (int)(input_requisition.dims[1]));

    g_print("ufo_dfi_sinc_task_get_requisition (complex == 2 floats): requisition->dims[0] = %d\n", (int)(requisition->dims[0]));
    g_print("ufo_dfi_sinc_task_get_requisition (number of rows): requisition->dims[1] = %d\n", (int)(requisition->dims[1]));
}

static void
ufo_dfi_sinc_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               UfoInputParam **in_params,
                               UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_PROCESSOR;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
}

static gboolean
ufo_dfi_sinc_task_process (UfoGpuTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition,
                         UfoGpuNode *node)
{
    UfoDfiSincTaskPrivate *priv;
    UfoRequisition input_requisition;
    cl_command_queue cmd_queue;
    cl_context context;

    cl_mem in_mem, out_mem, ktbl_mem;

    priv = UFO_DFI_SINC_TASK_GET_PRIVATE (task);
    cmd_queue = g_list_nth_data(ufo_resources_get_cmd_queues(priv->resources), 0);
    context = ufo_resources_get_context(priv->resources);
    
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    ktbl_mem = ufo_buffer_get_device_image(priv->ktbl_buffer, cmd_queue);

    ufo_buffer_get_requisition (inputs[0], &input_requisition);

    priv->L2 = ((cl_float)priv->L)/2.0f;
    priv->ktbl_len2 = (priv->number_presampled_values - 1)/2;
    priv->raster_size = input_requisition.dims[0]/2;
    priv->raster_size2 = priv->raster_size/2;
    priv->table_spacing = ((cl_float)priv->number_presampled_values)/((cl_float)priv->L);
    priv->angle_step_rad = M_PI/((cl_float)input_requisition.dims[1]);
    priv->theta_max = (cl_float)input_requisition.dims[1];
    priv->rho_max = (cl_float)input_requisition.dims[0]/2;

    int interp_grid_cols = ceil((float)priv->raster_size/(float)BLOCK_SIZE);
    if (priv->roi_size >= 1 && priv->roi_size <= priv->raster_size) {
        interp_grid_cols = ceil((float)priv->roi_size/(float)BLOCK_SIZE);
    }                    

    priv->spectrum_offset = (priv->raster_size - (interp_grid_cols * BLOCK_SIZE))/2;
    priv->max_radius = (interp_grid_cols * BLOCK_SIZE)/2.0;

    g_print("ufo_dfi_sinc_task_process:\n");
    g_print("L2 = %f\n", priv->L2);
    g_print("ktbl_len2 = %d\n", priv->ktbl_len2);
    g_print("raster_size = %d\n", priv->raster_size);
    g_print("raster_size2 = %d\n", priv->raster_size2);
    g_print("table_spacing = %f\n", priv->table_spacing);
    g_print("angle_step_rad = %f\n", priv->angle_step_rad);
    g_print("theta_max = %f\n", priv->theta_max);
    g_print("rho_max = %f\n", priv->rho_max);
    g_print("oversampling = %f\n", priv->oversampling);
    g_print("spectrum_offset = %d\n", priv->spectrum_offset);
    g_print("max_radius = %f\n", priv->max_radius);
    g_print("interp_grid_cols = %d\n", interp_grid_cols);

    size_t local_work_size[] = {BLOCK_SIZE, BLOCK_SIZE};

    ////////////////
    size_t empty_kernel_working_size[] = {priv->raster_size, priv->raster_size};

    CL_CHECK_ERROR (clSetKernelArg (priv->clear_kernel, 0, sizeof (cl_mem), &out_mem));
    CL_CHECK_ERROR (clEnqueueNDRangeKernel (cmd_queue,
                                            priv->clear_kernel,
                                            requisition->n_dims,
                                            NULL,
                                            empty_kernel_working_size,
                                            local_work_size,
                                            0, NULL, NULL));
    ////////////////
    size_t working_size[] = {interp_grid_cols * BLOCK_SIZE,
                             interp_grid_cols * BLOCK_SIZE};
    
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 0, sizeof (cl_mem), &in_mem));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 1, sizeof (cl_mem), &ktbl_mem));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 2, sizeof (cl_float), &(priv->L2)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 3, sizeof (cl_int), &(priv->ktbl_len2)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 4, sizeof (cl_int), &(priv->raster_size)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 5, sizeof (cl_int), &(priv->raster_size2)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 6, sizeof (cl_float), &(priv->table_spacing)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 7, sizeof (cl_float), &(priv->angle_step_rad)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 8, sizeof (cl_float), &(priv->theta_max)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 9, sizeof (cl_float), &(priv->rho_max)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 10, sizeof (cl_float), &(priv->max_radius)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 11, sizeof (cl_int), &(priv->spectrum_offset)));
    CL_CHECK_ERROR (clSetKernelArg (priv->dfi_sinc_kernel, 12, sizeof (cl_mem), &out_mem));

    CL_CHECK_ERROR (clEnqueueNDRangeKernel (cmd_queue,
                                            priv->dfi_sinc_kernel,
                                            requisition->n_dims,
                                            NULL,
                                            working_size,
                                            local_work_size,
                                            0, NULL, NULL));

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
        case PROP_OVERSAMPLING:
            priv->oversampling = g_value_get_uint (value);
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
        case PROP_OVERSAMPLING:
            g_value_set_uint (value, priv->oversampling);
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
    G_OBJECT_CLASS (ufo_dfi_sinc_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_dfi_sinc_task_setup;
    iface->get_structure = ufo_dfi_sinc_task_get_structure;
    iface->get_requisition = ufo_dfi_sinc_task_get_requisition;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_dfi_sinc_task_process;
}

static void
ufo_dfi_sinc_task_class_init (UfoDfiSincTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_dfi_sinc_task_set_property;
    gobject_class->get_property = ufo_dfi_sinc_task_get_property;
    gobject_class->finalize = ufo_dfi_sinc_task_finalize;

    properties[PROP_L] =
        g_param_spec_uint ("kernel-size",
            "Kernel size",
            "The length of kernel which will be used in gridding.",
            1, G_MAXUINT, 1,
            G_PARAM_READWRITE);

    properties[PROP_NUM_PRESAMPLED_VLS] =
        g_param_spec_uint ("number-presampled-values",
            "Number of presampled values",
            "Number of presampled values which will be used to calculate L kernel coefficients.",
            1, G_MAXUINT, 1,
            G_PARAM_READWRITE);

    properties[PROP_OVERSAMPLING] =
        g_param_spec_uint ("oversampling",
            "Oversampling",
            "Coefficient of oversampling.",
            1, G_MAXUINT, 1,
            G_PARAM_READWRITE);

    properties[PROP_ROI_SIZE] =
        g_param_spec_int ("roi-size",
            "Size of Region of Interest",
            "The length of one side of Region of Interest.",
            -1, G_MAXINT, -1,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoDfiSincTaskPrivate));
}

static void
ufo_dfi_sinc_task_init(UfoDfiSincTask *self)
{
    self->priv = UFO_DFI_SINC_TASK_GET_PRIVATE(self);
}
