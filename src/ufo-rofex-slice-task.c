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

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <string.h>
#include <math.h>
#include "ufo-rofex-slice-task.h"

/*
  Description:
  The filter splits a stack of incoming 2D images to push them forward one by
  one. It should be possible to identify to which plane each image relates.
  By this reason, if more than one image in the stack, the plane index is
  calculated.

  Input:
  A stack of 2D images, i.e. the stack of parallel-beam sinograms:
    0: nParDetectors
    1: nParProjections
    2: portionSize

  Output:
  A 2D image of the following dimensions:
    0: nParDetectors
    1: nParProjections
*/


struct _UfoRofexSliceTaskPrivate {
    guint n_planes;

    UfoBuffer *copy;
    gsize size;
    guint current;
    guint last;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexSliceTask, ufo_rofex_slice_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_SLICE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_SLICE_TASK, UfoRofexSliceTaskPrivate))

enum {
    PROP_0,
    PROP_N_PLANES,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_slice_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_SLICE_TASK, NULL));
}

static void
ufo_rofex_slice_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_rofex_slice_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexSliceTaskPrivate *priv;
    UfoRequisition in_req;

    ufo_buffer_get_requisition (inputs[0], &in_req);

    requisition->n_dims = 2;
    requisition->dims[0] = in_req.dims[0];
    requisition->dims[1] = in_req.dims[1];

    priv = UFO_ROFEX_SLICE_TASK_GET_PRIVATE (task);
    priv->current = 0;
    priv->last = in_req.dims[2];
    priv->size = in_req.dims[0] * in_req.dims[1] * sizeof(gfloat);
}

static guint
ufo_rofex_slice_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_slice_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_slice_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_slice_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexSliceTaskPrivate *priv;

    priv = UFO_ROFEX_SLICE_TASK_GET_PRIVATE (task);
    priv->copy = ufo_buffer_dup (inputs[0]);

    /* Force CPU memory */
    ufo_buffer_get_host_array (priv->copy, NULL);

    /* Move data */
    ufo_buffer_copy (inputs[0], priv->copy);
    ufo_buffer_copy_metadata (inputs[0], priv->copy);

    return FALSE;
}

static gboolean
ufo_rofex_slice_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexSliceTaskPrivate *priv;
    UfoRequisition copy_req;
    gfloat *src;
    gfloat *dst;

    priv = UFO_ROFEX_SLICE_TASK_GET_PRIVATE (task);

    if (priv->current == priv->last) {
        priv->current = 0;
        return FALSE;
    }

    src = ufo_buffer_get_host_array (priv->copy, NULL);
    dst = ufo_buffer_get_host_array (output, NULL);
    memcpy (dst, src + priv->current * priv->size / sizeof(gfloat), priv->size);

    ufo_buffer_get_requisition(priv->copy, &copy_req);
    ufo_buffer_copy_metadata (priv->copy, output);

    if (copy_req.n_dims > 2 && copy_req.dims[2] > 1) {
        guint plane_index;
        plane_index = priv->current % priv->n_planes;

        GValue gv_plane_index = {0};
        g_value_init (&gv_plane_index, G_TYPE_UINT);
        g_value_set_uint (&gv_plane_index, plane_index);
        ufo_buffer_set_metadata (output, "plane-index", &gv_plane_index);
    }

    priv->current++;

    return TRUE;
}

static void
ufo_rofex_slice_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexSliceTaskPrivate *priv;
    priv = UFO_ROFEX_SLICE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_slice_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexSliceTaskPrivate *priv;
    priv = UFO_ROFEX_SLICE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_slice_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_slice_task_parent_class)->finalize (object);
}

static void
ufo_rofex_slice_task_dispose (GObject *object)
{
    UfoRofexSliceTaskPrivate *priv;
    priv = UFO_ROFEX_SLICE_TASK_GET_PRIVATE (object);

    if (priv->copy) {
        g_object_unref (priv->copy);
        priv->copy = NULL;
    }

    G_OBJECT_CLASS (ufo_rofex_slice_task_parent_class)->dispose (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_slice_task_setup;
    iface->get_num_inputs = ufo_rofex_slice_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_slice_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_slice_task_get_mode;
    iface->get_requisition = ufo_rofex_slice_task_get_requisition;
    iface->process = ufo_rofex_slice_task_process;
    iface->generate = ufo_rofex_slice_task_generate;
}

static void
ufo_rofex_slice_task_class_init (UfoRofexSliceTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_slice_task_set_property;
    oclass->get_property = ufo_rofex_slice_task_get_property;
    oclass->finalize = ufo_rofex_slice_task_finalize;
    oclass->dispose = ufo_rofex_slice_task_dispose;

    properties[PROP_N_PLANES] =
                g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexSliceTaskPrivate));
}

static void
ufo_rofex_slice_task_init(UfoRofexSliceTask *self)
{
    self->priv = UFO_ROFEX_SLICE_TASK_GET_PRIVATE(self);
    self->priv->n_planes = 1;
}
