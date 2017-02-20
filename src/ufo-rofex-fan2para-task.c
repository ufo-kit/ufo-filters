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

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-rofex-fan2para-task.h"


struct _UfoRofexFan2paraTaskPrivate {
    guint n_planes;
    guint n_par_dets;
    guint n_par_proj;
    guint detector_diameter;

    cl_kernel interp_kernel;
    cl_kernel set_kernel;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexFan2paraTask, ufo_rofex_fan2para_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_FAN2PARA_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_FAN2PARA_TASK, UfoRofexFan2paraTaskPrivate))

enum {
    PROP_0,
    PROP_N_PLANES,
    PROP_N_PAR_DETECTORS,
    PROP_N_PAR_PROJECTIONS,
    PROP_DETECTOR_DIAMETER,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_fan2para_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_FAN2PARA_TASK, NULL));
}

static void
ufo_rofex_fan2para_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexFan2paraTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PARA_TASK_GET_PRIVATE (task);

    priv->interp_kernel = ufo_resources_get_kernel (resources, "rofex.cl", "fan2par_interp", error);
    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->interp_kernel));

    priv->set_kernel = ufo_resources_get_kernel (resources, "rofex.cl", "fan2par_set", error);
    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->set_kernel));
}

static void
ufo_rofex_fan2para_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexFan2paraTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PARA_TASK_GET_PRIVATE (task);

    requisition->n_dims = 2;
    requisition->dims[0] = priv->n_par_dets;
    requisition->dims[1] = priv->n_par_proj;
}

static guint
ufo_rofex_fan2para_task_get_num_inputs (UfoTask *task)
{
    return 2;
}

static guint
ufo_rofex_fan2para_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_rofex_fan2para_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_rofex_fan2para_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexFan2paraTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PARA_TASK_GET_PRIVATE (task);

    // Get command queue
    UfoGpuNode *node;
    cl_command_queue cmd_queue;
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    // On the first input we expect a fan-beam sinogram
    // On the second input we expect a set of parameters for the transformation
    gpointer d_fan_sino = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    gpointer d_params = ufo_buffer_get_device_array(inputs[1], cmd_queue);
    gpointer d_output = ufo_buffer_get_device_array(output, cmd_queue);

    // Compute param_offset
    guint param_offset = 0;
    UfoRequisition params_req;
    ufo_buffer_get_requisition(inputs[1], &params_req);
    if (params_req.n_dims != 2) {
        param_offset = priv->n_par_dets * priv->n_par_proj * priv->n_planes;
    } else {
        param_offset = params_req.dims[0];
    }

    // Get plane ID for the sinogram
    GValue *gv_plane_index;
    guint plane_index = 0;
    gv_plane_index = ufo_buffer_get_metadata (inputs[0], "plane-index");
    plane_index = g_value_get_uint (gv_plane_index);

    // Other params
    UfoRequisition fan_sino_req;
    ufo_buffer_get_requisition(inputs[0], &fan_sino_req);

    gfloat detector_r = priv->detector_diameter / 2.0;
    guint n_fan_dets = fan_sino_req.dims[0];
    guint n_fan_proj = fan_sino_req.dims[1];
    guint n_par_dets = requisition->dims[0];
    guint n_par_proj = requisition->dims[1];

    // Run the kernels
    UfoProfiler *profiler;
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->set_kernel, 0, sizeof (cl_mem), &d_output));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->set_kernel, 1, sizeof (guint), &n_par_dets));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->set_kernel, 2, sizeof (guint), &n_par_proj));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->set_kernel,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 0, sizeof (cl_mem), &d_fan_sino));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 1, sizeof (cl_mem), &d_output));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 2, sizeof (cl_mem), &d_params));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 3, sizeof (guint), &param_offset));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 4, sizeof (guint), &plane_index));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 5, sizeof (guint), &n_fan_dets));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 6, sizeof (guint), &n_fan_proj));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 7, sizeof (guint), &n_par_dets));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 8, sizeof (guint), &n_par_proj));
    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (priv->interp_kernel, 9, sizeof (gfloat), &detector_r));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->interp_kernel,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);

    return TRUE;
}


static void
ufo_rofex_fan2para_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFan2paraTaskPrivate *priv = UFO_ROFEX_FAN2PARA_TASK_GET_PRIVATE (object);
    switch (property_id) {
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
            break;
        case PROP_N_PAR_DETECTORS:
            priv->n_par_dets = g_value_get_uint(value);
            break;
        case PROP_N_PAR_PROJECTIONS:
            priv->n_par_proj = g_value_get_uint(value);
            break;
        case PROP_DETECTOR_DIAMETER:
            priv->detector_diameter = g_value_get_float(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_fan2para_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFan2paraTaskPrivate *priv = UFO_ROFEX_FAN2PARA_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
            break;
        case PROP_N_PAR_DETECTORS:
            g_value_set_uint(value, priv->n_par_dets);
            break;
        case PROP_N_PAR_PROJECTIONS:
            g_value_set_uint(value, priv->n_par_proj);
            break;
        case PROP_DETECTOR_DIAMETER:
            g_value_set_float(value, priv->detector_diameter);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_fan2para_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_fan2para_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_fan2para_task_setup;
    iface->get_num_inputs = ufo_rofex_fan2para_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_fan2para_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_fan2para_task_get_mode;
    iface->get_requisition = ufo_rofex_fan2para_task_get_requisition;
    iface->process = ufo_rofex_fan2para_task_process;
}

static void
ufo_rofex_fan2para_task_class_init (UfoRofexFan2paraTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_fan2para_task_set_property;
    oclass->get_property = ufo_rofex_fan2para_task_get_property;
    oclass->finalize = ufo_rofex_fan2para_task_finalize;

    properties[PROP_N_PLANES] =
                g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PAR_DETECTORS] =
                g_param_spec_uint ("number-of-parallel-detectors",
                                  "The number of pixels in a parallel projection",
                                  "The number of pixels in a parallel projection",
                                  1, G_MAXUINT, 256,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PAR_PROJECTIONS] =
                g_param_spec_uint ("number-of-parallel-projections",
                                  "The number of parallel projection",
                                  "The number of parallel projection",
                                  1, G_MAXUINT, 512,
                                  G_PARAM_READWRITE);

    properties[PROP_DETECTOR_DIAMETER] =
                g_param_spec_float ("detector-diameter",
                                    "Detector diameter.",
                                    "Detector diameter.",
                                    0, G_MAXFLOAT, 216.0,
                                    G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexFan2paraTaskPrivate));
}

static void
ufo_rofex_fan2para_task_init(UfoRofexFan2paraTask *self)
{
    self->priv = UFO_ROFEX_FAN2PARA_TASK_GET_PRIVATE(self);
    self->priv->n_planes = 1;
    self->priv->n_par_dets = 256;
    self->priv->n_par_proj = 512;
    self->priv->detector_diameter = 216.0;
}
