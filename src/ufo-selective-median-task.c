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

#include "ufo-selective-median-task.h"

/**
 * SECTION:ufo-median-filter-task
 * @Short_description: Write TIFF files
 * @Title: selective_median
 *
 */

struct _UfoSelectiveMedianTaskPrivate {
    cl_kernel inner_kernel;
    cl_kernel fill_kernel;
    guint size;
    gfloat threshold;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoSelectiveMedianTask, ufo_selective_median_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_SELECTIVE_MEDIAN_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_SELECTIVE_MEDIAN_TASK, UfoSelectiveMedianTaskPrivate))

enum {
    PROP_0,
    PROP_SIZE,
    PROP_THRESHOLD,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_selective_median_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_SELECTIVE_MEDIAN_TASK, NULL));
}

static void
ufo_selective_median_task_setup (UfoTask *task,
                              UfoResources *resources,
                              GError **error)
{
    UfoSelectiveMedianTaskPrivate *priv;
    gchar *option;

    priv = UFO_SELECTIVE_MEDIAN_TASK_GET_PRIVATE (task);
    option = g_strdup_printf (" -DMEDIAN_BOX_SIZE=%i -DTHRESHOLD=%f", priv->size, priv->threshold);
    priv->inner_kernel = ufo_resources_get_kernel (resources, "selective-median.cl",
            "filter_inner", option, error);

    priv->fill_kernel = ufo_resources_get_kernel (resources, "selective-median.cl",
            "fill", option, error);

    if (priv->inner_kernel != NULL)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->inner_kernel), error);

    if (priv->fill_kernel != NULL)
        UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->fill_kernel), error);

    g_free (option);
}

static void
ufo_selective_median_task_get_requisition (UfoTask *task,
                                        UfoBuffer **inputs,
                                        UfoRequisition *requisition,
                                        GError **error)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_selective_median_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_selective_median_task_get_num_dimensions (UfoTask *task,
                                           guint input)
{
    return 2;
}

static UfoTaskMode
ufo_selective_median_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_selective_median_task_process (UfoTask *task,
                                UfoBuffer **inputs,
                                UfoBuffer *output,
                                UfoRequisition *requisition)
{
    UfoSelectiveMedianTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;

    size_t inner_size[2];

    priv = UFO_SELECTIVE_MEDIAN_TASK_GET_PRIVATE (task);

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->fill_kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->fill_kernel, 1, sizeof (cl_mem), &out_mem));

    ufo_profiler_call (profiler, cmd_queue, priv->fill_kernel, 2, requisition->dims, NULL);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->inner_kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->inner_kernel, 1, sizeof (cl_mem), &out_mem));

    inner_size[0] = requisition->dims[0] - (priv->size - 1);
    inner_size[1] = requisition->dims[1] - (priv->size - 1);

    ufo_profiler_call (profiler, cmd_queue, priv->inner_kernel, 2, inner_size, NULL);

    return TRUE;
}

static void
ufo_selective_median_task_set_property (GObject *object,
                                     guint property_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
    UfoSelectiveMedianTaskPrivate *priv;

    priv = UFO_SELECTIVE_MEDIAN_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SIZE:
            {
                guint new_size;

                new_size = g_value_get_uint (value);

                if ((new_size % 2) == 0)
                    g_warning ("SelectiveMedian::size = %i is divisible by 2, ignoring it", new_size);
                else
                    priv->size = new_size;
            }
	    break;
    case PROP_THRESHOLD:
      {
	gfloat new_threshold;
	new_threshold = g_value_get_float(value);
	priv->threshold = new_threshold;
      }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_selective_median_task_get_property (GObject *object,
                                     guint property_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
    UfoSelectiveMedianTaskPrivate *priv;

    priv = UFO_SELECTIVE_MEDIAN_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SIZE:
            g_value_set_uint (value, priv->size);
            break;
    case PROP_THRESHOLD:
      g_value_set_float(value, priv->threshold);
      break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_selective_median_task_finalize (GObject *object)
{
    UfoSelectiveMedianTaskPrivate *priv;

    priv = UFO_SELECTIVE_MEDIAN_TASK_GET_PRIVATE (object);

    if (priv->inner_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->inner_kernel));
        priv->inner_kernel = NULL;
    }

    if (priv->fill_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->fill_kernel));
        priv->fill_kernel = NULL;
    }

    G_OBJECT_CLASS (ufo_selective_median_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_selective_median_task_setup;
    iface->get_num_inputs = ufo_selective_median_task_get_num_inputs;
    iface->get_num_dimensions = ufo_selective_median_task_get_num_dimensions;
    iface->get_mode = ufo_selective_median_task_get_mode;
    iface->get_requisition = ufo_selective_median_task_get_requisition;
    iface->process = ufo_selective_median_task_process;
}

static void
ufo_selective_median_task_class_init (UfoSelectiveMedianTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_selective_median_task_set_property;
    gobject_class->get_property = ufo_selective_median_task_get_property;
    gobject_class->finalize = ufo_selective_median_task_finalize;

    properties[PROP_SIZE] =
        g_param_spec_uint ("size",
            "Size of median box",
            "Size of median box",
            3, 33, 3,
            G_PARAM_READWRITE);

    properties[PROP_THRESHOLD] =
      g_param_spec_float ("threshold",
			  "threshold for selction",
			  "threshold for selection",
			  0, 1, 0.2,
			  G_PARAM_READWRITE);
    
    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoSelectiveMedianTaskPrivate));
}

static void
ufo_selective_median_task_init(UfoSelectiveMedianTask *self)
{
    self->priv = UFO_SELECTIVE_MEDIAN_TASK_GET_PRIVATE(self);
    self->priv->size = 3;
}
