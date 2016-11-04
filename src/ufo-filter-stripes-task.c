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

#include "ufo-filter-stripes-task.h"
#include "common/ufo-fft.h"


struct _UfoFilterStripesTaskPrivate {
    UfoFft *forward;
    UfoFft *inverse;
    UfoFftParameter forward_params;
    UfoFftParameter inverse_params;
    cl_context context;
    cl_kernel kernel;
    cl_kernel spread;
    cl_kernel pack;
    cl_mem temp;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFilterStripesTask, ufo_filter_stripes_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_FILTER_STRIPES_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_STRIPES_TASK, UfoFilterStripesTaskPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

static guint32
pow2round(guint32 x)
{
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x+1;
}

UfoNode *
ufo_filter_stripes_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FILTER_STRIPES_TASK, NULL));
}

static gboolean
ufo_filter_stripes_task_process (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoBuffer *output,
                                 UfoRequisition *requisition)
{
    UfoFilterStripesTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_int width;
    cl_int height;
    gfloat scale;
    size_t global_work_size[3];
    gfloat pattern = 0.0f;

    priv = UFO_FILTER_STRIPES_TASK (task)->priv;
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    width = (cl_int) requisition->dims[0];
    height = (cl_int) requisition->dims[1];

    global_work_size[0] = priv->forward_params.size[0];
    global_work_size[1] = priv->forward_params.size[1];
    global_work_size[2] = 1;

    /* clear everything for unknown reason ... */
    UFO_RESOURCES_CHECK_CLERR (clEnqueueFillBuffer (cmd_queue, out_mem, &pattern, sizeof (pattern),
                                                    0, width * height * 4, 0, NULL, NULL));

    UFO_RESOURCES_CHECK_CLERR (clEnqueueFillBuffer (cmd_queue, priv->temp, &pattern, sizeof (pattern),
                                                    0, global_work_size[0] * global_work_size[1] * 4, 0, NULL, NULL));

    /* forward transform */
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread, 0, sizeof (cl_mem), (gpointer) &priv->temp));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread, 1, sizeof (cl_mem), (gpointer) &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread, 2, sizeof (cl_int), &width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->spread, 3, sizeof (cl_int), &height));

    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue, priv->spread,
                                                       3, NULL, global_work_size, NULL,
                                                       0, NULL, NULL));

    UFO_RESOURCES_CHECK_CLERR (clFinish (cmd_queue));

    UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (priv->forward, cmd_queue, profiler,
                                                priv->temp, priv->temp, UFO_FFT_FORWARD,
                                                0, NULL, NULL));

    UFO_RESOURCES_CHECK_CLERR (clFinish (cmd_queue));

    /* remove frequencies */
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), (gpointer) &priv->temp));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), (gpointer) &priv->temp));

    ufo_profiler_call (profiler, cmd_queue, priv->kernel, 2, global_work_size, NULL);

    UFO_RESOURCES_CHECK_CLERR (clFinish (cmd_queue));

    /* inverse transform */
    UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (priv->forward, cmd_queue, profiler,
                                                priv->temp, priv->temp, UFO_FFT_BACKWARD,
                                                0, NULL, NULL));


    scale = 1.0f / global_work_size[0] / global_work_size[1];

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack, 0, sizeof (cl_mem), (gpointer) &priv->temp));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack, 1, sizeof (cl_mem), (gpointer) &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack, 2, sizeof (cl_int), &width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack, 3, sizeof (cl_int), &height));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->pack, 4, sizeof (gfloat), &scale));

    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue, priv->pack,
                                                       3, NULL, global_work_size, NULL,
                                                       0, NULL, NULL));
    UFO_RESOURCES_CHECK_CLERR (clFinish (cmd_queue));

    return TRUE;
}

static void
ufo_filter_stripes_task_setup (UfoTask *task,
                               UfoResources *resources,
                               GError **error)
{
    UfoFilterStripesTaskPrivate *priv;

    priv = UFO_FILTER_STRIPES_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    priv->kernel = ufo_resources_get_kernel (resources, "filter.cl", "stripe_filter", error);

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));

    priv->spread = ufo_resources_get_kernel (resources, "fft.cl", "fft_spread", error);

    if (priv->spread != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->spread));

    priv->pack = ufo_resources_get_kernel (resources, "fft.cl", "fft_pack", error);

    if (priv->pack != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->pack));
}

static void
ufo_filter_stripes_task_get_requisition (UfoTask *task,
                                         UfoBuffer **inputs,
                                         UfoRequisition *requisition)
{
    UfoFilterStripesTaskPrivate *priv;
    UfoRequisition in_req;
    cl_command_queue queue;

    priv = UFO_FILTER_STRIPES_TASK_GET_PRIVATE (task);

    ufo_buffer_get_requisition (inputs[0], &in_req);
    ufo_buffer_get_requisition (inputs[0], requisition);

    priv->forward_params.dimensions = UFO_FFT_2D;
    priv->forward_params.zeropad = TRUE;
    priv->forward_params.size[0] = pow2round (in_req.dims[0]);
    priv->forward_params.size[1] = pow2round (in_req.dims[1]);
    priv->forward_params.size[2] = 1;
    priv->forward_params.batch = 1;

    priv->inverse_params.dimensions = UFO_FFT_2D;
    priv->inverse_params.zeropad = FALSE;
    priv->inverse_params.size[0] = priv->forward_params.size[0];
    priv->inverse_params.size[1] = priv->forward_params.size[1];
    priv->inverse_params.size[2] = 1;
    priv->inverse_params.batch = 1;

    queue = ufo_gpu_node_get_cmd_queue (UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task))));

    UFO_RESOURCES_CHECK_CLERR (ufo_fft_update (priv->forward, priv->context, queue, &priv->forward_params));
    UFO_RESOURCES_CHECK_CLERR (ufo_fft_update (priv->inverse, priv->context, queue, &priv->inverse_params));

    if (priv->temp == NULL) {
        /* TODO: check if size has changed and re-allocate */
        cl_int error;
        size_t size;

        size = 2 * priv->forward_params.size[0] * priv->forward_params.size[1] * sizeof(gfloat);

        priv->temp = clCreateBuffer (priv->context, CL_MEM_READ_WRITE, size, NULL, &error);
        UFO_RESOURCES_CHECK_CLERR (error);
    }
}

static guint
ufo_filter_stripes_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_filter_stripes_task_get_num_dimensions (UfoTask *task,
                                            guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_filter_stripes_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static void
ufo_filter_stripes_task_finalize (GObject *object)
{
    UfoFilterStripesTaskPrivate *priv;

    priv = UFO_FILTER_STRIPES_TASK_GET_PRIVATE (object);

    if (priv->temp) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->temp));
        priv->temp = NULL;
    }

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    if (priv->forward) {
        ufo_fft_destroy (priv->forward);
        priv->forward = NULL;
    }

    if (priv->inverse) {
        ufo_fft_destroy (priv->inverse);
        priv->inverse = NULL;
    }

    G_OBJECT_CLASS (ufo_filter_stripes_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_filter_stripes_task_setup;
    iface->get_requisition = ufo_filter_stripes_task_get_requisition;
    iface->get_num_inputs = ufo_filter_stripes_task_get_num_inputs;
    iface->get_num_dimensions = ufo_filter_stripes_task_get_num_dimensions;
    iface->get_mode = ufo_filter_stripes_task_get_mode;
    iface->process = ufo_filter_stripes_task_process;
}

static void
ufo_filter_stripes_task_class_init (UfoFilterStripesTaskClass *klass)
{
    GObjectClass *oclass;

    oclass = G_OBJECT_CLASS (klass);
    oclass->finalize = ufo_filter_stripes_task_finalize;
    g_type_class_add_private(klass, sizeof(UfoFilterStripesTaskPrivate));
}

static void
ufo_filter_stripes_task_init (UfoFilterStripesTask *self)
{
    UfoFilterStripesTaskPrivate *priv;
    self->priv = priv = UFO_FILTER_STRIPES_TASK_GET_PRIVATE (self);
    priv->kernel = NULL;
    priv->forward = ufo_fft_new ();
    priv->inverse = ufo_fft_new ();
    priv->temp = NULL;
}
