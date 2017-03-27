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

#include <stdio.h>
#include "ufo-rofex-process-darks-task.h"

/*
  This filter should be used after reordering stage. It averages dark fields
  along the projections and frames.

  It is assumed that filter receives a 3D stack of the following dimensions:
    0: nDetsPerModule * nDetModules
    1: nProjections
    2: nPlanes * nFrames

  As result it produces a data set of dimensions:
    0: nDetsPerModule * nDetModules
    1: nPlanes
*/

// Averaging
void
average_darks (gfloat *darks,
               gfloat *avg,
               guint  n_dets,
               guint  n_proj,
               guint  n_planes,
               guint  n_frames);

// Interplation
void
interp_avg_darks (gfloat *darks,
                  guint  n_dets,
                  guint  n_planes);

struct _UfoRofexProcessDarksTaskPrivate {
    guint  n_planes;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexProcessDarksTask, ufo_rofex_process_darks_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_PROCESS_DARKS_TASK, UfoRofexProcessDarksTaskPrivate))

enum {
    PROP_0,
    PROP_N_PLANES,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_process_darks_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_PROCESS_DARKS_TASK, NULL));
}

static void
ufo_rofex_process_darks_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_rofex_process_darks_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexProcessDarksTaskPrivate *priv;
    priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE (task);

    ufo_buffer_get_requisition(inputs[0], requisition);

    requisition->n_dims = 2;
    requisition->dims[1] = priv->n_planes;
}

static guint
ufo_rofex_process_darks_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_process_darks_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_process_darks_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_process_darks_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexProcessDarksTaskPrivate *priv;
    UfoRequisition req;

    priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    guint n_frames = req.dims[2] / priv->n_planes;
    guint n_fan_dets = req.dims[0];
    guint n_fan_proj = req.dims[1];

    gfloat *darks = ufo_buffer_get_host_array(inputs[0], NULL);
    gfloat *avg_darks = ufo_buffer_get_host_array(output, NULL);
    memset(avg_darks, 0, ufo_buffer_get_size(output));

    average_darks (darks,
                   avg_darks,
                   n_fan_dets,
                   n_fan_proj,
                   priv->n_planes,
                   n_frames);

    interp_avg_darks (avg_darks,
                      n_fan_dets,
                      priv->n_planes);

    return TRUE;
}

static void
ufo_rofex_process_darks_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexProcessDarksTaskPrivate *priv;
    priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE (object);

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
ufo_rofex_process_darks_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexProcessDarksTaskPrivate *priv;
    priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE (object);

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
ufo_rofex_process_darks_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_process_darks_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_process_darks_task_setup;
    iface->get_num_inputs = ufo_rofex_process_darks_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_process_darks_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_process_darks_task_get_mode;
    iface->get_requisition = ufo_rofex_process_darks_task_get_requisition;
    iface->process = ufo_rofex_process_darks_task_process;
}

static void
ufo_rofex_process_darks_task_class_init (UfoRofexProcessDarksTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_process_darks_task_set_property;
    oclass->get_property = ufo_rofex_process_darks_task_get_property;
    oclass->finalize = ufo_rofex_process_darks_task_finalize;

    properties[PROP_N_PLANES] =
                g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexProcessDarksTaskPrivate));
}

static void
ufo_rofex_process_darks_task_init(UfoRofexProcessDarksTask *self)
{
    self->priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE(self);
    self->priv->n_planes = 1;
}

// Averaging
void
average_darks (gfloat *darks,
               gfloat *avg,
               guint  n_fan_dets,
               guint  n_fan_proj,
               guint  n_planes,
               guint  n_frames)
{
    gfloat factor = 1.0 / (gfloat) (n_frames * n_fan_proj);
    guint idx = 0;
    guint plane_offset;
    for (guint frame = 0; frame < n_frames; frame++) {
        for (guint plane = 0; plane < n_planes; plane++) {
            plane_offset = (frame * n_planes + plane) * (n_fan_proj * n_fan_dets);
            for (guint proj = 0; proj < n_fan_proj; proj++) {
                for (guint det = 0; det < n_fan_dets; det++) {
                    idx = det + proj * n_fan_dets + plane_offset;
                    avg[det + plane * n_fan_dets] += darks[idx] * factor;
                }
            }
        }
    }
}

// Interplation
void
interp_avg_darks (gfloat *avg,
                  guint  n_fan_dets,
                  guint  n_planes)
{
    gfloat val = 0.0;
    guint idxL, idxR;
    for (guint plane = 0; plane < n_planes; plane++) {
        for (guint det = 0; det < n_fan_dets; det++) {
            if (avg[det + plane * n_fan_dets] > 300) {
                idxR = plane * n_fan_dets + (det + 1) % n_fan_dets;
                idxL = plane * n_fan_dets + (det - 1) % n_fan_dets;

                val = avg[idxL] + avg[idxR];
                avg[det + plane * n_fan_dets] = 0.5 * val;
            }
        }
    }
}
