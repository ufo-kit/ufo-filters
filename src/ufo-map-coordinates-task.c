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

#include "common/ufo-addressing.h"
#include "common/ufo-interpolation.h"
#include "ufo-map-coordinates-task.h"


struct _UfoMapCoordinatesTaskPrivate {
    AddressingMode addressing_mode;
    Interpolation interpolation;

    cl_context context;
    cl_kernel kernel;
    cl_sampler sampler;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoMapCoordinatesTask, ufo_map_coordinates_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_MAP_COORDINATES_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_MAP_COORDINATES_TASK, UfoMapCoordinatesTaskPrivate))

enum {
    PROP_0,
    PROP_INTERPOLATION,
    PROP_ADDRESSING_MODE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_map_coordinates_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_MAP_COORDINATES_TASK, NULL));
}

static void
ufo_map_coordinates_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoMapCoordinatesTaskPrivate *priv;
    cl_int cl_error;

    priv = UFO_MAP_COORDINATES_TASK_GET_PRIVATE (task);
    priv->context = ufo_resources_get_context (resources);
    priv->kernel = ufo_resources_get_kernel (resources, "interpolator.cl", "map_coordinates", NULL, error);
    /* Normalized coordinates are necessary for repeat addressing mode */
    priv->sampler = clCreateSampler (priv->context, (cl_bool) FALSE, priv->addressing_mode, priv->interpolation, &cl_error);

    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainContext (priv->context), error);
    UFO_RESOURCES_CHECK_SET_AND_RETURN (cl_error, error);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->kernel), error);
    }
}

static void
ufo_map_coordinates_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition,
                                 GError **error)
{
    ufo_buffer_get_requisition (inputs[0], requisition);

    if (ufo_buffer_cmp_dimensions (inputs[1], requisition) != 0 ||
        ufo_buffer_cmp_dimensions (inputs[2], requisition) != 0) {
        g_set_error_literal (error, UFO_TASK_ERROR, UFO_TASK_ERROR_GET_REQUISITION,
                             "map-coordinates inputs must have the same size");
    }
}

static guint
ufo_map_coordinates_task_get_num_inputs (UfoTask *task)
{
    return 3;
}

static guint
ufo_map_coordinates_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_map_coordinates_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_map_coordinates_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoMapCoordinatesTaskPrivate *priv;
    UfoProfiler *profiler;
    UfoGpuNode *node;
    cl_command_queue cmd_queue;
    cl_mem in_mem, x_mem, y_mem;
    cl_mem out_mem;

    priv = UFO_MAP_COORDINATES_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_image (inputs[0], cmd_queue);
    x_mem = ufo_buffer_get_device_array (inputs[1], cmd_queue);
    y_mem = ufo_buffer_get_device_array (inputs[2], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_sampler), &priv->sampler));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (cl_mem), &x_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 4, sizeof (cl_mem), &y_mem));

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, priv->kernel, 2, requisition->dims, NULL);

    return TRUE;
}


static void
ufo_map_coordinates_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoMapCoordinatesTaskPrivate *priv = UFO_MAP_COORDINATES_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_INTERPOLATION:
            priv->interpolation = g_value_get_enum (value);
            break;
        case PROP_ADDRESSING_MODE:
            priv->addressing_mode = g_value_get_enum (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_map_coordinates_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoMapCoordinatesTaskPrivate *priv = UFO_MAP_COORDINATES_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_INTERPOLATION:
            g_value_set_enum (value, priv->interpolation);
            break;
        case PROP_ADDRESSING_MODE:
            g_value_set_enum (value, priv->addressing_mode);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_map_coordinates_task_finalize (GObject *object)
{
    UfoMapCoordinatesTaskPrivate *priv;

    priv = UFO_MAP_COORDINATES_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->sampler) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseSampler (priv->sampler));
        priv->sampler = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    G_OBJECT_CLASS (ufo_map_coordinates_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_map_coordinates_task_setup;
    iface->get_num_inputs = ufo_map_coordinates_task_get_num_inputs;
    iface->get_num_dimensions = ufo_map_coordinates_task_get_num_dimensions;
    iface->get_mode = ufo_map_coordinates_task_get_mode;
    iface->get_requisition = ufo_map_coordinates_task_get_requisition;
    iface->process = ufo_map_coordinates_task_process;
}

static void
ufo_map_coordinates_task_class_init (UfoMapCoordinatesTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_map_coordinates_task_set_property;
    oclass->get_property = ufo_map_coordinates_task_get_property;
    oclass->finalize = ufo_map_coordinates_task_finalize;

    properties[PROP_ADDRESSING_MODE] =
        g_param_spec_enum ("addressing-mode",
            "Outlier treatment (\"none\", \"clamp\", \"clamp_to_edge\", \"repeat\", \"mirrored_repeat\")",
            "Outlier treatment (\"none\", \"clamp\", \"clamp_to_edge\", \"repeat\", \"mirrored_repeat\")",
            g_enum_register_static ("ufo_map_coords_addressing_mode", addressing_values),
            CL_ADDRESS_CLAMP,
            G_PARAM_READWRITE);

    properties[PROP_INTERPOLATION] =
        g_param_spec_enum ("interpolation",
            "Interpolation (\"nearest\" or \"linear\")",
            "Interpolation (\"nearest\" or \"linear\")",
            g_enum_register_static ("ufo_map_coords_interpolation", interpolation_values),
            CL_FILTER_LINEAR,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoMapCoordinatesTaskPrivate));
}

static void
ufo_map_coordinates_task_init(UfoMapCoordinatesTask *self)
{
    self->priv = UFO_MAP_COORDINATES_TASK_GET_PRIVATE(self);
    self->priv->addressing_mode = CL_ADDRESS_CLAMP;
    self->priv->interpolation = CL_FILTER_LINEAR;
}
