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

#include "ufo-downsample-task.h"


struct _UfoDownsampleTaskPrivate {
    cl_kernel fast_kernel;
    guint factor;
    guint target_width;
    guint target_height;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoDownsampleTask, ufo_downsample_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_DOWNSAMPLE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_DOWNSAMPLE_TASK, UfoDownsampleTaskPrivate))

enum {
    PROP_0,
    PROP_FACTOR,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_downsample_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_DOWNSAMPLE_TASK, NULL));
}

static void
ufo_downsample_task_setup (UfoTask *task,
                           UfoResources *resources,
                           GError **error)
{
    UfoDownsampleTaskPrivate *priv;

    priv = UFO_DOWNSAMPLE_TASK_GET_PRIVATE (task);
    priv->fast_kernel = ufo_resources_get_kernel (resources, "downsample.cl", "downsample_fast", error);

    if (priv->fast_kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->fast_kernel));
}

static void
ufo_downsample_task_get_requisition (UfoTask *task,
                                     UfoBuffer **inputs,
                                     UfoRequisition *requisition)
{
    UfoDownsampleTaskPrivate *priv;
    UfoRequisition in_req;

    priv = UFO_DOWNSAMPLE_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &in_req);

    requisition->n_dims = 2;
    requisition->dims[0] = priv->target_width = in_req.dims[0] / priv->factor;
    requisition->dims[1] = priv->target_height = in_req.dims[1] / priv->factor;
}

static void
ufo_downsample_task_get_structure (UfoTask *task,
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
ufo_downsample_task_process (UfoGpuTask *task,
                             UfoBuffer **inputs,
                             UfoBuffer *output,
                             UfoRequisition *requisition)
{
    UfoDownsampleTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue *cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;

    priv = UFO_DOWNSAMPLE_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE(task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->fast_kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->fast_kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->fast_kernel, 2, sizeof (guint), &priv->factor));

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, priv->fast_kernel, 2, requisition->dims, NULL);

    return TRUE;
}

static void
ufo_downsample_task_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
    UfoDownsampleTaskPrivate *priv = UFO_DOWNSAMPLE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FACTOR:
            priv->factor = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_downsample_task_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
    UfoDownsampleTaskPrivate *priv = UFO_DOWNSAMPLE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FACTOR:
            g_value_set_uint (value, priv->factor);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_downsample_task_finalize (GObject *object)
{
    UfoDownsampleTaskPrivate *priv;

    priv = UFO_DOWNSAMPLE_TASK_GET_PRIVATE (object);

    if (priv->fast_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->fast_kernel));
        priv->fast_kernel = NULL;
    }

    G_OBJECT_CLASS (ufo_downsample_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_downsample_task_setup;
    iface->get_structure = ufo_downsample_task_get_structure;
    iface->get_requisition = ufo_downsample_task_get_requisition;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_downsample_task_process;
}

static void
ufo_downsample_task_class_init (UfoDownsampleTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_downsample_task_set_property;
    gobject_class->get_property = ufo_downsample_task_get_property;
    gobject_class->finalize = ufo_downsample_task_finalize;

    properties[PROP_FACTOR] =
        g_param_spec_uint ("factor",
                           "Downsample factor",
                           "Downsample factor, e.g. 2 reduces width and height by 2",
                           2, 16, 2,
                           G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoDownsampleTaskPrivate));
}

static void
ufo_downsample_task_init(UfoDownsampleTask *self)
{
    self->priv = UFO_DOWNSAMPLE_TASK_GET_PRIVATE(self);
    self->priv->factor = 2;
}
