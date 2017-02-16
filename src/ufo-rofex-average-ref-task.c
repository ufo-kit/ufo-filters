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

#include "ufo-rofex-average-ref-task.h"


struct _UfoRofexAverageRefTaskPrivate {
    guint n_planes;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexAverageRefTask, ufo_rofex_average_ref_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_AVERAGE_REF_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_AVERAGE_REF_TASK, UfoRofexAverageRefTaskPrivate))

enum {
    PROP_0,
    PROP_N_PLANES,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_average_ref_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_AVERAGE_REF_TASK, NULL));
}

static void
ufo_rofex_average_ref_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_rofex_average_ref_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
  UfoRofexAverageRefTaskPrivate *priv = UFO_ROFEX_AVERAGE_REF_TASK_GET_PRIVATE (task);

  UfoRequisition in_req;
  ufo_buffer_get_requisition(inputs[0], &in_req);

  requisition->n_dims = 3;
  requisition->dims[0] = in_req.dims[0];
  requisition->dims[1] = in_req.dims[1];
  requisition->dims[2] = priv->n_planes;
}

static guint
ufo_rofex_average_ref_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_average_ref_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_average_ref_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_average_ref_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexAverageRefTaskPrivate *priv = UFO_ROFEX_AVERAGE_REF_TASK_GET_PRIVATE (task);

    UfoRequisition in_req;
    ufo_buffer_get_requisition(inputs[0], &in_req);

    if (in_req.n_dims < 3) {
        g_error("Nothing to average. Please pass 3D Data.");
        return FALSE;
    }

    guint n_dets = in_req.dims[0];
    guint n_proj = in_req.dims[1];
    guint n_planes = priv->n_planes;
    guint n_slices = in_req.dims[2] / n_planes;

    gfloat *h_sino = ufo_buffer_get_host_array(inputs[0], NULL);
    gfloat *h_average = ufo_buffer_get_host_array(output, NULL);

    gfloat factor = 1.0 / (gfloat)n_slices;
    guint n_vals = n_dets * n_proj;
    // factor = 0.0
    for (guint sliceInd = 0; sliceInd < n_slices; sliceInd++) {
      for (guint planeInd = 0; planeInd < priv->n_planes; planeInd++) {
        for (guint index = 0; index < n_vals ; index++)
        {
          guint valInd = (sliceInd  + planeInd) * n_vals + index;

          gfloat val = (gfloat)h_sino[valInd];
          guint insertInd = index + planeInd * n_vals;

          h_average[index + planeInd * n_vals] += val * factor;
        }
      }
    }

    return TRUE;
}


static void
ufo_rofex_average_ref_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexAverageRefTaskPrivate *priv = UFO_ROFEX_AVERAGE_REF_TASK_GET_PRIVATE (object);

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
ufo_rofex_average_ref_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexAverageRefTaskPrivate *priv = UFO_ROFEX_AVERAGE_REF_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            g_value_set_uint(value, priv->n_planes);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_average_ref_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_average_ref_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_average_ref_task_setup;
    iface->get_num_inputs = ufo_rofex_average_ref_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_average_ref_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_average_ref_task_get_mode;
    iface->get_requisition = ufo_rofex_average_ref_task_get_requisition;
    iface->process = ufo_rofex_average_ref_task_process;
}

static void
ufo_rofex_average_ref_task_class_init (UfoRofexAverageRefTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_average_ref_task_set_property;
    oclass->get_property = ufo_rofex_average_ref_task_get_property;
    oclass->finalize = ufo_rofex_average_ref_task_finalize;

    properties[PROP_N_PLANES] =
              g_param_spec_uint ("number-of-planes",
                                 "The number of planes",
                                 "The number of planes",
                                 1, G_MAXUINT, 1,
                                 G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexAverageRefTaskPrivate));
}

static void
ufo_rofex_average_ref_task_init(UfoRofexAverageRefTask *self)
{
    self->priv = UFO_ROFEX_AVERAGE_REF_TASK_GET_PRIVATE(self);
    self->priv->n_planes = 1;
}
