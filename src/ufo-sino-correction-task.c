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

#include "ufo-sino-correction-task.h"

/**
 * SECTION:ufo-sino-correction-task
 * @Short_description: Write TIFF files
 * @Title: sino_correction
 *
 */

struct _UfoSinoCorrectionTaskPrivate {
    cl_context context;
    cl_kernel flat_correct_kernel;
    cl_kernel absorptivity_kernel;
    gboolean absorptivity;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoSinoCorrectionTask, ufo_sino_correction_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_SINO_CORRECTION_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_SINO_CORRECTION_TASK, UfoSinoCorrectionTaskPrivate))

enum {
    PROP_0,
    PROP_ABSORPTIVITY,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_sino_correction_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_SINO_CORRECTION_TASK, NULL));
}

static void
ufo_sino_correction_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoSinoCorrectionTaskPrivate *priv;

    priv = UFO_SINO_CORRECTION_TASK_GET_PRIVATE (task);
    
    priv->context = ufo_resources_get_context (resources);
    priv->flat_correct_kernel = ufo_resources_get_kernel (resources, "sino-correction.cl", "flat_correct", error);
    priv->absorptivity_kernel = ufo_resources_get_kernel (resources, "sino-correction.cl", "absorptivity", error);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    if (priv->flat_correct_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->flat_correct_kernel));
    }

    if (priv->absorptivity_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->absorptivity_kernel));
    }
}

static void
ufo_sino_correction_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static void
ufo_sino_correction_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               UfoInputParam **in_params,
                               UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_PROCESSOR;
    *n_inputs = 3;
    *in_params = g_new0 (UfoInputParam, 3);
    /* A sinogram */
    (*in_params)[0].n_dims = 2;
    /* A row of dark frame */
    (*in_params)[1].n_dims = 1;
    /* A row of flat frame */
    (*in_params)[2].n_dims = 1;
}

static gboolean
ufo_sino_correction_task_process (UfoGpuTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoSinoCorrectionTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem, dark_mem, flat_mem;
    cl_mem out_mem;
    cl_kernel kernel;

    priv = UFO_SINO_CORRECTION_TASK (task)->priv;
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    dark_mem = ufo_buffer_get_device_array (inputs[1], cmd_queue);
    flat_mem = ufo_buffer_get_device_array (inputs[2], cmd_queue);

    if (priv->absorptivity) {
        kernel = priv->absorptivity_kernel;
    }
    else {
        kernel = priv->flat_correct_kernel;
    }

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 2, sizeof (cl_mem), &dark_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 3, sizeof (cl_mem), &flat_mem));

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, kernel, 2, requisition->dims, NULL);

    return TRUE;
}

static void
ufo_sino_correction_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoSinoCorrectionTaskPrivate *priv = UFO_SINO_CORRECTION_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_ABSORPTIVITY:
            priv->absorptivity = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_sino_correction_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoSinoCorrectionTaskPrivate *priv = UFO_SINO_CORRECTION_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_ABSORPTIVITY:
            g_value_set_boolean (value, priv->absorptivity);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_sino_correction_task_finalize (GObject *object)
{
    UfoSinoCorrectionTaskPrivate *priv;
    priv = UFO_SINO_CORRECTION_TASK_GET_PRIVATE (object);

    if (priv->flat_correct_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->flat_correct_kernel));
    }

    if (priv->absorptivity_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->absorptivity_kernel));
    }

    G_OBJECT_CLASS (ufo_sino_correction_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_sino_correction_task_setup;
    iface->get_structure = ufo_sino_correction_task_get_structure;
    iface->get_requisition = ufo_sino_correction_task_get_requisition;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_sino_correction_task_process;
}

static void
ufo_sino_correction_task_class_init (UfoSinoCorrectionTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_sino_correction_task_set_property;
    gobject_class->get_property = ufo_sino_correction_task_get_property;
    gobject_class->finalize = ufo_sino_correction_task_finalize;

    properties[PROP_ABSORPTIVITY] =
        g_param_spec_boolean ("absorption-correction",
            "Absorption correction",
            "Absorption correction",
            FALSE,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoSinoCorrectionTaskPrivate));
}

static void
ufo_sino_correction_task_init(UfoSinoCorrectionTask *self)
{
    UfoSinoCorrectionTaskPrivate *priv;
    self->priv = priv = UFO_SINO_CORRECTION_TASK_GET_PRIVATE(self);

    priv->flat_correct_kernel = NULL;
    priv->absorptivity_kernel = NULL;
    priv->absorptivity = FALSE;
}
