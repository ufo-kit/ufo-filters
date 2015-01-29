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
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library. If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <sys/types.h>
#include "ufo-scalability-task.h"
#include <ufo/ufo.h>

static void ufo_task_interface_init (UfoTaskIface *iface);
G_DEFINE_TYPE_WITH_CODE (UfoScalabilityTask, ufo_scalability_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_SCALABILITY_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_SCALABILITY_TASK, UfoScalabilityTaskPrivate))

struct _UfoScalabilityTaskPrivate {
    UfoPluginManager *plugin_manager;
    UfoResources     *resources;
    cl_command_queue cmd_queue;
    gpointer kernel;
};

enum {
    PROP_0,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = {NULL, };

UfoNode *
ufo_scalability_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_SCALABILITY_TASK, NULL));
}

static void
ufo_scalability_task_setup (UfoTask      *task,
                   UfoResources *resources,
                   GError       **error)
{
    UfoScalabilityTaskPrivate *priv = UFO_SCALABILITY_TASK_GET_PRIVATE (task);
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));

    UfoProfiler *profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    priv->resources = g_object_ref (resources);
    priv->cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    UFO_RESOURCES_CHECK_CLERR (clRetainCommandQueue (priv->cmd_queue));
    priv->kernel = ufo_resources_get_kernel (resources, "ufo-scal-test.cl", "test", error);
}

static UfoNode *
ufo_scalability_task_node_copy (UfoNode *node,
                       GError **error)
{
    UfoNode *copy = UFO_NODE (ufo_scalability_task_new());
    UfoScalabilityTaskPrivate *priv = UFO_SCALABILITY_TASK_GET_PRIVATE (node);
    return copy;
}

static void
ufo_scalability_task_get_requisition (UfoTask        *task,
                             UfoBuffer      **inputs,
                             UfoRequisition *requisition)
{
    UfoScalabilityTaskPrivate *priv = UFO_SCALABILITY_TASK_GET_PRIVATE (task);

    UfoRequisition in_req;
    ufo_buffer_get_requisition (inputs[0], &in_req);

    requisition->n_dims = in_req.n_dims;
    for (int i = 0; i < in_req.n_dims; i++)
        requisition->dims[i] = in_req.dims[i];
}

static guint
ufo_scalability_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_scalability_task_get_num_dimensions (UfoTask *task,
                                guint   input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_scalability_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}


static gboolean
ufo_scalability_task_process (UfoTask        *task,
                     UfoBuffer      **inputs,
                     UfoBuffer      *output,
                     UfoRequisition *requisition)
{
    //g_print ("\n Task %p  thread: %p", task, g_thread_self());
    GTimer *process_time = g_timer_new();
    GTimer *loop_time = g_timer_new ();

    g_timer_start (process_time);

    UfoScalabilityTaskPrivate *priv = UFO_SCALABILITY_TASK_GET_PRIVATE (task);
    ufo_op_set (output, 0, priv->resources, priv->cmd_queue);
    UfoProfiler *profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    UfoRequisition input_req;
    ufo_buffer_get_requisition (inputs[0], &input_req);

    cl_mem d_input = ufo_buffer_get_device_image (inputs[0], priv->cmd_queue);
    cl_mem d_output = ufo_buffer_get_device_image (output, priv->cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof(cl_mem), &d_input));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof(cl_mem), &d_output));

    cl_ulong total_t = 0;
    double sec_time = 0.0;
    cl_ulong total_rt = 0;
    double sec_rtime = 0.0;
    g_timer_start (loop_time);
    for (long i = 0 ; i < 500000; ++i) {
        cl_event event = NULL;
        //ufo_profiler_call (profiler, priv->cmd_queue, priv->kernel, input_req.n_dims,
        //        input_req.dims, &event);

        clEnqueueNDRangeKernel (priv->cmd_queue,
                priv->kernel,
            input_req.n_dims,
                NULL,
                input_req.dims,
                NULL,
                0,
                NULL, NULL);

        //clWaitForEvents(1, &event);

        /*
        cl_ulong t1=0, t2=0;
        clGetEventProfilingInfo (event, CL_PROFILING_COMMAND_QUEUED, sizeof (cl_ulong), &t1, NULL);
        clGetEventProfilingInfo (event, CL_PROFILING_COMMAND_START, sizeof (cl_ulong), &t2, NULL);
        total_t += t2 - t1;
        sec_time += ((double)(t2-t1)) / 1000000000;

        clGetEventProfilingInfo (event, CL_PROFILING_COMMAND_START, sizeof (cl_ulong), &t1, NULL);
        clGetEventProfilingInfo (event, CL_PROFILING_COMMAND_END, sizeof (cl_ulong), &t2, NULL);
        total_rt += t2 - t1;
        sec_rtime += ((double)(t2-t1)) / 1000000000;
        */
        clFinish(priv->cmd_queue);
    }
    g_timer_stop (loop_time);
    g_timer_stop (process_time);

    gdouble process_t = g_timer_elapsed (process_time, NULL);
    gdouble loop_t = g_timer_elapsed (loop_time, NULL);

     g_print ("\n Task %p CMD_Q: %p  QUEUED-START: %f s. START-END: %f s. LOOP TIME: %f  PROCESS TIME: %f", task, priv->cmd_queue, sec_time, sec_rtime, loop_t, process_t); 
    return TRUE;
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_scalability_task_setup;
    iface->get_num_inputs = ufo_scalability_task_get_num_inputs;
    iface->get_num_dimensions = ufo_scalability_task_get_num_dimensions;
    iface->get_mode = ufo_scalability_task_get_mode;
    iface->get_requisition = ufo_scalability_task_get_requisition;
    iface->process = ufo_scalability_task_process;
}

static void
ufo_scalability_task_set_property (GObject      *object,
                          guint        property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    UfoScalabilityTaskPrivate *priv = UFO_SCALABILITY_TASK_GET_PRIVATE (object);

    switch (property_id) {
       default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_scalability_task_get_property (GObject    *object,
                          guint      property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    UfoScalabilityTaskPrivate *priv = UFO_SCALABILITY_TASK_GET_PRIVATE (object);

    switch (property_id) {
      default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_scalability_task_dispose (GObject *object)
{
    UfoScalabilityTaskPrivate *priv = UFO_SCALABILITY_TASK_GET_PRIVATE (object);

    g_clear_object (&priv->resources);
    G_OBJECT_CLASS (ufo_scalability_task_parent_class)->dispose (object);
}

static void
ufo_scalability_task_finalize (GObject *object)
{
    UfoScalabilityTaskPrivate *priv = UFO_SCALABILITY_TASK_GET_PRIVATE (object);
    if (priv->cmd_queue)
      UFO_RESOURCES_CHECK_CLERR (clReleaseCommandQueue(priv->cmd_queue));

    G_OBJECT_CLASS (ufo_scalability_task_parent_class)->finalize (object);
}

static void
ufo_scalability_task_class_init (UfoScalabilityTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->set_property = ufo_scalability_task_set_property;
    gobject_class->get_property = ufo_scalability_task_get_property;
    gobject_class->finalize = ufo_scalability_task_finalize;
    gobject_class->dispose = ufo_scalability_task_dispose;

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
          g_object_class_install_property (gobject_class, i, properties[i]);

      g_type_class_add_private (gobject_class, sizeof(UfoScalabilityTaskPrivate));

      UFO_NODE_CLASS (klass)->copy = ufo_scalability_task_node_copy;
}

static void
ufo_scalability_task_init(UfoScalabilityTask *self)
{
    UfoScalabilityTaskPrivate *priv = NULL;
    self->priv = priv = UFO_SCALABILITY_TASK_GET_PRIVATE(self);
    priv->plugin_manager = ufo_plugin_manager_new ();
    priv->resources = NULL;
    priv->cmd_queue = NULL;
    priv->kernel = NULL;
}
