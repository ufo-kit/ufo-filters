/*
 * Copyright (C) 2016 Karlsruhe Institute of Technology
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

#include "ufo-segment-task.h"

typedef struct {
    int x;
    int y;
} Label;

struct _UfoSegmentTaskPrivate {
    cl_context context;
    cl_kernel kernel;
    UfoBuffer *labeled;
    guint current;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoSegmentTask, ufo_segment_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_SEGMENT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_SEGMENT_TASK, UfoSegmentTaskPrivate))

UfoNode *
ufo_segment_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_SEGMENT_TASK, NULL));
}

static void
ufo_segment_task_setup (UfoTask *task,
                        UfoResources *resources,
                        GError **error)
{
    UfoSegmentTaskPrivate *priv;

    priv = UFO_SEGMENT_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    priv->kernel = ufo_resources_get_kernel (resources, "segment.cl", "segment", error);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
}

static void
ufo_segment_task_get_requisition (UfoTask *task,
                                  UfoBuffer **inputs,
                                  UfoRequisition *requisition)
{
    UfoRequisition label_req;

    ufo_buffer_get_requisition (inputs[0], requisition);
    requisition->n_dims = 2;

    /* ensure inputs match */
    ufo_buffer_get_requisition (inputs[1], &label_req);

    if ((label_req.dims[0] != requisition->dims[0]) ||
        (label_req.dims[1] != requisition->dims[1])) {
        g_warning ("Label field and input dimensions do not match ([%zu, %zu] != [%zu, %zu])",
                   label_req.dims[0], label_req.dims[1], requisition->dims[0], requisition->dims[1]);
    }
}

static guint
ufo_segment_task_get_num_inputs (UfoTask *task)
{
    return 2;
}

static guint
ufo_segment_task_get_num_dimensions (UfoTask *task,
                                     guint input)
{
    if (input == 0)
        return 3;

    return 2;
}

static UfoTaskMode
ufo_segment_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_GPU;
}

static Label *
extract_labels (UfoBuffer *buffer, guint *num_labels)
{
    UfoRequisition requisition;
    gfloat *data;
    Label *result;
    guint width;
    guint height;
    guint num = 0;
    guint num_allocated = 4096;

    ufo_buffer_get_requisition (buffer, &requisition);
    data = ufo_buffer_get_host_array (buffer, NULL);
    result = malloc (sizeof (Label) * num_allocated);
    width = requisition.dims[0];
    height = requisition.dims[1];

    for (guint x = 0; x < width; x++) {
        for (guint y = 0; y < height; y++) {
            if (data[y * width + x] > 0.0f) {
                result[num].x = x;
                result[num].y = y;
                num++;

                if (num == num_allocated) {
                    num_allocated += 4096;
                    result = realloc (result, num_allocated * sizeof (Label));
                }
            }
        }
    }

    *num_labels = num;
    return result;
}

static gboolean
ufo_segment_task_process (UfoTask *task,
                          UfoBuffer **inputs,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoSegmentTaskPrivate *priv;
    UfoRequisition in_req;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    Label *prelabeled_host;
    cl_mem prelabeled_device;
    gfloat *random_host;
    cl_mem random_device;
    cl_mem labeled;
    cl_mem slices;
    cl_int error;
    guint width;
    guint height;
    guint num_slices;
    gsize work_size;
    guint num_labels = 0;
    gfloat fill_pattern = 0.0f;

    priv = UFO_SEGMENT_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    /* extract labels and create GPU memory */
    prelabeled_host = extract_labels (inputs[1], &num_labels);
    prelabeled_device = clCreateBuffer (priv->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                        num_labels * sizeof (Label), prelabeled_host, &error);
    UFO_RESOURCES_CHECK_CLERR (error);

    /* create uniformly distributed data */
    random_host = g_malloc0 (32768 * sizeof (gfloat));

    for (guint i = 0; i < 32768; i++)
        random_host[i] = (gfloat) g_random_double ();

    random_device = clCreateBuffer (priv->context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                    32768 * sizeof (gfloat), random_host, &error);

    UFO_RESOURCES_CHECK_CLERR (error);

    ufo_buffer_get_requisition (inputs[0], &in_req);

    priv->labeled = ufo_buffer_dup (inputs[0]);
    width = in_req.dims[0];
    height = in_req.dims[1];
    num_slices = in_req.dims[2];
    priv->current = in_req.dims[2];

    labeled = ufo_buffer_get_device_array (priv->labeled, cmd_queue);
    slices = ufo_buffer_get_device_array (inputs[0], cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clEnqueueFillBuffer (cmd_queue, labeled, &fill_pattern, sizeof (fill_pattern), 0, ufo_buffer_get_size (priv->labeled), 0, NULL, NULL));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &slices));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &labeled));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_mem), &prelabeled_device));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (guint), &width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 4, sizeof (guint), &height));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 5, sizeof (guint), &num_slices));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 6, sizeof (cl_mem), &random_device));

    work_size = num_labels;

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, priv->kernel, 1, &work_size, NULL);

    UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (prelabeled_device));
    UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (random_device));
    free (prelabeled_host);
    free (random_host);

    return TRUE;
}

static gboolean
ufo_segment_task_generate (UfoTask *task,
                           UfoBuffer *output,
                           UfoRequisition *requisition)
{
    UfoSegmentTaskPrivate *priv;
    UfoRequisition req;
    UfoGpuNode *node;
    cl_command_queue cmd_queue;
    cl_mem labeled;
    cl_mem out_mem;
    cl_event event;
    gsize offset;
    gsize size;

    priv = UFO_SEGMENT_TASK_GET_PRIVATE (task);

    if (priv->current == 0) {
        g_object_unref (priv->labeled);
        return FALSE;
    }

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    labeled = ufo_buffer_get_device_array (priv->labeled, cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    ufo_buffer_get_requisition (priv->labeled, &req);
    size = ufo_buffer_get_size (output);
    offset = (req.dims[2] - priv->current) * size;

    UFO_RESOURCES_CHECK_CLERR (clEnqueueCopyBuffer (cmd_queue,
                                                    labeled, out_mem,
                                                    offset, 0, size,
                                                    0, NULL, &event));

    UFO_RESOURCES_CHECK_CLERR (clWaitForEvents (1, &event));
    UFO_RESOURCES_CHECK_CLERR (clReleaseEvent (event));

    priv->current--;
    return TRUE;
}

static void
ufo_segment_task_finalize (GObject *object)
{
    UfoSegmentTaskPrivate *priv;

    priv = UFO_SEGMENT_TASK_GET_PRIVATE (object);

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));

    UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));

    G_OBJECT_CLASS (ufo_segment_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_segment_task_setup;
    iface->get_num_inputs = ufo_segment_task_get_num_inputs;
    iface->get_num_dimensions = ufo_segment_task_get_num_dimensions;
    iface->get_mode = ufo_segment_task_get_mode;
    iface->get_requisition = ufo_segment_task_get_requisition;
    iface->process = ufo_segment_task_process;
    iface->generate = ufo_segment_task_generate;
}

static void
ufo_segment_task_class_init (UfoSegmentTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->finalize = ufo_segment_task_finalize;

    g_type_class_add_private (oclass, sizeof(UfoSegmentTaskPrivate));
}

static void
ufo_segment_task_init(UfoSegmentTask *self)
{
    self->priv = UFO_SEGMENT_TASK_GET_PRIVATE(self);
}
