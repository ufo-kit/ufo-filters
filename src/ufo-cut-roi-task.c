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

#include "ufo-cut-roi-task.h"

struct _UfoCutRoiTaskPrivate {
    guint x;
    guint y;
    guint width;
    guint height;

    cl_command_queue cmd_queue;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoCutRoiTask, ufo_cut_roi_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_CUT_ROI_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_CUT_ROI_TASK, UfoCutRoiTaskPrivate))

enum {
    PROP_0,
    PROP_X,
    PROP_Y,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_cut_roi_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_CUT_ROI_TASK, NULL));
}

static void
ufo_cut_roi_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoCutRoiTaskPrivate *priv;
    UfoGpuNode *node;

    priv = UFO_CUT_ROI_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));

    priv->cmd_queue = ufo_gpu_node_get_cmd_queue (node);
}

static void
ufo_cut_roi_task_get_requisition (UfoTask *task,
                                             UfoBuffer **inputs,
                                             UfoRequisition *requisition)
{
    UfoCutRoiTaskPrivate *priv;

    priv = UFO_CUT_ROI_TASK_GET_PRIVATE (task);
    requisition->n_dims = 2;
    requisition->dims[0] = priv->width;
    requisition->dims[1] = priv->height;
}

static guint
ufo_cut_roi_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_cut_roi_task_get_num_dimensions (UfoTask *task,
                               guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_cut_roi_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_cut_roi_task_process (UfoTask *task,
                                     UfoBuffer **inputs,
                                     UfoBuffer *output,
                                     UfoRequisition *requisition)
{
    UfoCutRoiTaskPrivate *priv;
    UfoRequisition req;
    guint x1, y1, x2, y2;
    guint rd_width, rd_height;
    guint in_width, in_height;
    cl_mem in_data;
    cl_mem out_data;

    priv = UFO_CUT_ROI_TASK_GET_PRIVATE (task);
    x1 = priv->x;
    y1 = priv->y;
    x2 = x1 + priv->width;
    y2 = y1 + priv->height;

    ufo_buffer_get_requisition (inputs[0], &req);

    in_width = (guint) req.dims[0];
    in_height = (guint) req.dims[1];

    /* Don't do anything if we are completely out of bounds */
    if (x1 > in_width || y1 > in_height) {
        g_warning ("%i > %i or %i > %i", x1, in_width, y1, in_height);
        return FALSE;
    }

    rd_width = x2 > in_width ? in_width - x1 : priv->width;
    rd_height = y2 > in_height ? in_height - y1 : priv->height;

    in_data = ufo_buffer_get_device_array (inputs[0], priv->cmd_queue);
    out_data = ufo_buffer_get_device_array (output, priv->cmd_queue);

    const size_t src_origin[3] = {x1 * sizeof(float), y1, 0};
    const size_t dst_origin[3] = {0, 0, 0};
    const size_t region[3] = {rd_width * sizeof(float), rd_height, 1}; 
    
    clEnqueueCopyBufferRect(priv->cmd_queue,
                            in_data,
                            out_data,
                            src_origin,
                            dst_origin,
                            region,
                            in_width * sizeof(float), 0,
                            rd_width * sizeof(float), 0,
                            0, NULL, NULL);

    return TRUE;
}

static void
ufo_cut_roi_task_set_property (GObject *object,
                                          guint property_id,
                                          const GValue *value,
                                          GParamSpec *pspec)
{
    UfoCutRoiTaskPrivate *priv = UFO_CUT_ROI_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_X:
            priv->x = g_value_get_uint (value);
            break;
        case PROP_Y:
            priv->y = g_value_get_uint (value);
            break;
        case PROP_WIDTH:
            priv->width = g_value_get_uint (value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_cut_roi_task_get_property (GObject *object,
                                          guint property_id,
                                          GValue *value,
                                          GParamSpec *pspec)
{
    UfoCutRoiTaskPrivate *priv = UFO_CUT_ROI_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_X:
            g_value_set_uint (value, priv->x);
            break;
        case PROP_Y:
            g_value_set_uint (value, priv->y);
            break;
        case PROP_WIDTH:
            g_value_set_uint (value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint (value, priv->height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_cut_roi_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_cut_roi_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_cut_roi_task_setup;
    iface->get_num_inputs = ufo_cut_roi_task_get_num_inputs;
    iface->get_num_dimensions = ufo_cut_roi_task_get_num_dimensions;
    iface->get_mode = ufo_cut_roi_task_get_mode;
    iface->get_requisition = ufo_cut_roi_task_get_requisition;
    iface->process = ufo_cut_roi_task_process;
}

static void
ufo_cut_roi_task_class_init (UfoCutRoiTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_cut_roi_task_set_property;
    gobject_class->get_property = ufo_cut_roi_task_get_property;
    gobject_class->finalize = ufo_cut_roi_task_finalize;

    properties[PROP_X] = 
        g_param_spec_uint("x",
            "Horizontal coordinate",
            "Horizontal coordinate from where to read input",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_Y] = 
        g_param_spec_uint("y",
            "Vertical coordinate",
            "Vertical coordinate from where to read input",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    properties[PROP_WIDTH] = 
        g_param_spec_uint("width",
            "Width",
            "Width of the region of interest",
            1, G_MAXUINT, 256,
            G_PARAM_READWRITE);

    properties[PROP_HEIGHT] = 
        g_param_spec_uint("height",
            "Height",
            "Height of the region of interest",
            1, G_MAXUINT, 256,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoCutRoiTaskPrivate));
}

static void
ufo_cut_roi_task_init(UfoCutRoiTask *self)
{
    self->priv = UFO_CUT_ROI_TASK_GET_PRIVATE(self);
    self->priv->x = 0;
    self->priv->y = 0;
    self->priv->width = 256;
    self->priv->height = 256;
}
