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
#include <math.h>
#include "ufo-flat-field-correction-task.h"


struct _UfoFlatFieldCorrectionTaskPrivate {
    gboolean fix_nan_and_inf;
    cl_kernel kernel;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFlatFieldCorrectionTask, ufo_flat_field_correction_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FLAT_FIELD_CORRECTION_TASK, UfoFlatFieldCorrectionTaskPrivate))

enum {
    PROP_0,
    PROP_FIX_NAN_AND_INF,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_flat_field_correction_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FLAT_FIELD_CORRECTION_TASK, NULL));
}

static void
ufo_flat_field_correction_task_setup (UfoTask *task,
                                      UfoResources *resources,
                                      GError **error)
{
    UfoFlatFieldCorrectionTaskPrivate *priv;

    priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (task);
    priv->kernel = ufo_resources_get_kernel (resources, "ffc.cl", "flat_field_correct", error);

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
}

static void
ufo_flat_field_correction_task_get_requisition (UfoTask *task,
                                                UfoBuffer **inputs,
                                                UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_flat_field_correction_task_get_num_inputs (UfoTask *task)
{
    return 3;
}

static guint
ufo_flat_field_correction_task_get_num_dimensions (UfoTask *task, guint input)
{
    g_return_val_if_fail (input <= 2, 0);
    return 2;
}

static UfoTaskMode
ufo_flat_field_correction_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_flat_field_correction_task_process (UfoTask *task,
                                        UfoBuffer **inputs,
                                        UfoBuffer *output,
                                        UfoRequisition *requisition)
{
    UfoFlatFieldCorrectionTaskPrivate *priv;
    UfoProfiler *profiler;
    UfoGpuNode *node;

    cl_command_queue cmd_queue;
    cl_mem proj_mem;
    cl_mem dark_mem;
    cl_mem flat_mem;
    cl_mem out_mem;

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    proj_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    dark_mem = ufo_buffer_get_device_array (inputs[1], cmd_queue);
    flat_mem = ufo_buffer_get_device_array (inputs[2], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (task);
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &proj_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_mem), &dark_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (cl_mem), &flat_mem));

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, priv->kernel, 2, requisition->dims, NULL);

#if 0
    gfloat *proj_data;
    gfloat *dark_data;
    gfloat *flat_data;
    gfloat *out_data;
    gfloat corrected_value;
    gsize n_pixels;

    priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (task);

    proj_data = ufo_buffer_get_host_array (inputs[0], NULL);
    dark_data = ufo_buffer_get_host_array (inputs[1], NULL);
    flat_data = ufo_buffer_get_host_array (inputs[2], NULL);
    out_data = ufo_buffer_get_host_array (output, NULL);
    n_pixels = requisition->dims[0] * requisition->dims[1];
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    ufo_profiler_start (profiler, UFO_PROFILER_TIMER_CPU);

    /* Flat field correction */
    for (gsize i = 0; i < n_pixels; i++) {
        corrected_value = (proj_data[i] - dark_data[i]) / (flat_data[i] - dark_data[i]);
        if (priv->fix_nan_and_inf && (isnan (out_data[i]) || isinf (out_data[i]))) {
            corrected_value = 0.0;
        }
        out_data[i] = corrected_value;
    }

    ufo_profiler_stop (profiler, UFO_PROFILER_TIMER_CPU);
#endif

    return TRUE;
}

static void
ufo_flat_field_correction_task_set_property (GObject *object,
                                             guint property_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
    UfoFlatFieldCorrectionTaskPrivate *priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FIX_NAN_AND_INF:
            priv->fix_nan_and_inf = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_flat_field_correction_task_get_property (GObject *object,
                                             guint property_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
    UfoFlatFieldCorrectionTaskPrivate *priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FIX_NAN_AND_INF:
            g_value_set_boolean (value, priv->fix_nan_and_inf);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_flat_field_correction_task_finalize (GObject *object)
{
    UfoFlatFieldCorrectionTaskPrivate *priv;

    priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    G_OBJECT_CLASS (ufo_flat_field_correction_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_flat_field_correction_task_setup;
    iface->get_num_inputs = ufo_flat_field_correction_task_get_num_inputs;
    iface->get_num_dimensions = ufo_flat_field_correction_task_get_num_dimensions;
    iface->get_mode = ufo_flat_field_correction_task_get_mode;
    iface->get_requisition = ufo_flat_field_correction_task_get_requisition;
    iface->process = ufo_flat_field_correction_task_process;
}

static void
ufo_flat_field_correction_task_class_init (UfoFlatFieldCorrectionTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_flat_field_correction_task_set_property;
    gobject_class->get_property = ufo_flat_field_correction_task_get_property;
    gobject_class->finalize = ufo_flat_field_correction_task_finalize;

    properties[PROP_FIX_NAN_AND_INF] =
        g_param_spec_boolean("fix-nan-and-inf",
                             "Replace NAN and INF values with 0.0",
                             "Replace NAN and INF values with 0.0",
                             FALSE,
                             G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoFlatFieldCorrectionTaskPrivate));
}

static void
ufo_flat_field_correction_task_init(UfoFlatFieldCorrectionTask *self)
{
    self->priv = UFO_FLAT_FIELD_CORRECTION_TASK_GET_PRIVATE(self);
    self->priv->fix_nan_and_inf = FALSE;
    self->priv->kernel = NULL;
}
