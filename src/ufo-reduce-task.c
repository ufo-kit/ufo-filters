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

#include <math.h>
#include <glib.h>
#include <glib/gprintf.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-reduce-task.h"

typedef enum {
    MODE_SUM,
    MODE_MEAN,
    MODE_MIN,
    MODE_MAX,
} Mode;

static GEnumValue mode_values[] = {
    { MODE_SUM,  "MODE_SUM",  "sum" },
    { MODE_MEAN, "MODE_SUM", "mean" },
    { MODE_MIN,  "MODE_MIN",  "min" },
    { MODE_MAX,  "MODE_MAX",  "max" },
    { 0, NULL, NULL}
};

struct _UfoReduceTaskPrivate {
    gsize local_size;
    Mode mode;
    cl_context context;
    cl_kernel kernel;
    cl_mem result;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoReduceTask, ufo_reduce_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_REDUCE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_REDUCE_TASK, UfoReduceTaskPrivate))

enum {
    PROP_0,
    PROP_MODE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_reduce_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_REDUCE_TASK, NULL));
}

static void
ufo_reduce_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    GValue *local_size_gvalue;
    UfoReduceTaskPrivate *priv = UFO_REDUCE_TASK_GET_PRIVATE (task);
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    local_size_gvalue = ufo_gpu_node_get_info (node, UFO_GPU_NODE_INFO_MAX_WORK_GROUP_SIZE);
    priv->local_size = g_value_get_ulong (local_size_gvalue);
    g_value_unset (local_size_gvalue);

    priv->result = NULL;
    priv->kernel = ufo_resources_get_kernel (resources, "reductor.cl",
                                             g_strconcat ("reduce_", mode_values[priv->mode].value_name, NULL),
                                             error);
    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
    }

    priv->context = ufo_resources_get_context (resources);
    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));
}

static void
ufo_reduce_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_reduce_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_reduce_task_get_num_dimensions (UfoTask *task,
                                    guint input)
{
    return -1;
}

static UfoTaskMode
ufo_reduce_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gfloat
reduce (UfoProfiler *profiler,
        cl_command_queue cmd_queue,
        cl_kernel kernel,
        cl_mem input,
        cl_mem output,
        gint size,
        gsize local_size)
{
    gint real_size, num_groups, pixels_per_thread;
    gsize global_work_size;
    gfloat result;

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 1, sizeof (cl_mem), &output));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 2, local_size * sizeof (cl_float), NULL));

    /* Balance the load and process multiple times until the global reduction
     * result is stored in the first pixel. One work item in the kernel
     * processes more pixels (global work size is thus less than the input
     * size). At the same time, we try to have many groups in order to have good
     * occupancy. */
    num_groups = size;
    while (num_groups > 1) {
        real_size = num_groups;
        /* Make sure global work size divides the local work size */
        /* Number of groups processing *real_size* data */
        num_groups = (real_size - 1) / local_size + 1;
        /* Balance the load, i.e. half of the work goes to work items, half to
         * groups. This way the GPU is busy on the work item level (because
         * every work item processes more input pixels) and occupancy is good
         * because there are many groups. */
        pixels_per_thread = (gint) (ceil (sqrt (num_groups)));
        num_groups = (num_groups - 1) / pixels_per_thread + 1;
        global_work_size = num_groups * local_size;
        g_debug ("real size: %d global size: %lu d G: %d PPT: %d",
                 real_size, global_work_size, num_groups, pixels_per_thread);
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 0, sizeof (cl_mem), &input));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 3, sizeof (cl_int), &real_size));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 4, sizeof (cl_int), &pixels_per_thread));
        ufo_profiler_call (profiler, cmd_queue, kernel, 1, &global_work_size, &local_size);
        input = output;
    }

    /* Result is stored in the first pixel */
    UFO_RESOURCES_CHECK_CLERR (clEnqueueReadBuffer (cmd_queue,
                                                    output,
                                                    CL_TRUE,
                                                    0,
                                                    sizeof (cl_float),
                                                    &result,
                                                    0, NULL, NULL));

    return result;
}

static gboolean
ufo_reduce_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoReduceTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_int error;
    guint i;
    gint num_groups, pixels_per_thread, input_size = 1;
    gfloat result;
    GValue meta = G_VALUE_INIT;

    priv = UFO_REDUCE_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    /* Arbitrary input dimensions are allowed */
    for (i = 0; i < requisition->n_dims; i++) {
        input_size *= requisition->dims[i]; 
    }

    if (!priv->result) {
        num_groups = (input_size - 1) / priv->local_size + 1;
        pixels_per_thread = (gint) (ceil (sqrt (num_groups)));
        num_groups = (num_groups - 1) / pixels_per_thread + 1;
        priv->result = clCreateBuffer (priv->context,
                                       CL_MEM_READ_WRITE,
                                       num_groups * sizeof (cl_float),
                                       NULL,
                                       &error);
        UFO_RESOURCES_CHECK_CLERR (error);
    }

    result = reduce (profiler, cmd_queue, priv->kernel, in_mem, priv->result, input_size, priv->local_size);

    if (priv->mode == MODE_MEAN) {
        result /= input_size;
    }

    /* Pass the original data on intact */
    ufo_buffer_swap_data (inputs[0], output);

    /* Set metadata */
    g_value_init (&meta, G_TYPE_FLOAT);
    g_value_set_float (&meta, result);
    ufo_buffer_set_metadata (output, mode_values[priv->mode].value_nick, &meta);
    g_value_unset (&meta);

    return TRUE;
}


static void
ufo_reduce_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoReduceTaskPrivate *priv = UFO_REDUCE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_MODE:
            priv->mode = g_value_get_enum (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_reduce_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoReduceTaskPrivate *priv = UFO_REDUCE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_MODE:
            g_value_set_enum (value, priv->mode);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_reduce_task_finalize (GObject *object)
{
    UfoReduceTaskPrivate *priv = UFO_REDUCE_TASK_GET_PRIVATE (object);

    if (priv->result) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->result));
        priv->result = NULL;
    }

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    G_OBJECT_CLASS (ufo_reduce_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_reduce_task_setup;
    iface->get_num_inputs = ufo_reduce_task_get_num_inputs;
    iface->get_num_dimensions = ufo_reduce_task_get_num_dimensions;
    iface->get_mode = ufo_reduce_task_get_mode;
    iface->get_requisition = ufo_reduce_task_get_requisition;
    iface->process = ufo_reduce_task_process;
}

static void
ufo_reduce_task_class_init (UfoReduceTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_reduce_task_set_property;
    oclass->get_property = ufo_reduce_task_get_property;
    oclass->finalize = ufo_reduce_task_finalize;

    properties[PROP_MODE] =
        g_param_spec_enum ("mode",
            "Mode (min, max, sum, mean)",
            "Mode (min, max, sum, mean)",
            g_enum_register_static ("mode", mode_values),
            MODE_SUM, G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoReduceTaskPrivate));
}

static void
ufo_reduce_task_init(UfoReduceTask *self)
{
    self->priv = UFO_REDUCE_TASK_GET_PRIVATE(self);
    self->priv->mode = MODE_SUM;
}
