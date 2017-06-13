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

#include <math.h>
#include <string.h>
#include <glib.h>
#include "ufo-rofex-process-darks-task.h"
#include "rofex.h"

/*
  DESCRIPTION:
  The filter averages dark fields along the projections and transitions.

  INPUT:
  A stack of 2D images:
    0: nModsPerRing * nDetsPerModule
    1: nFanProjections
    2: nTransPerPortion * ringsSelectionMaskSize

  OUTPUT:
  A 2D image:
    0: nModsPerRing * nDetsPerModule
    1: nRings
*/

struct _UfoRofexProcessDarksTaskPrivate {
    guint n_rings;
    GValueArray *gv_beam_positions;
    GValueArray *gv_rings_selection_mask;

    gint *rings_selection_mask;
    guint rings_selection_mask_size;

    guint *beam_positions;
    guint n_beam_positions;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexProcessDarksTask, ufo_rofex_process_darks_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_PROCESS_DARKS_TASK, UfoRofexProcessDarksTaskPrivate))

enum {
    PROP_0,
    PROP_N_RINGS,
    PROP_BEAM_POSITIONS,
    PROP_RINGS_SELECTION_MASK,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

void
average_darks (gfloat *darks,
               gfloat *avg,
               guint portion,
               guint n_trans_per_portion,
               guint n_fan_dets,
               guint n_fan_proj,
               guint n_fan_sinos,
               guint n_rings,
               gint  *rings_selection_mask,
               guint rings_selection_mask_size,
               guint *beam_positions,
               guint n_beam_positions);

void
interp_avg_darks (gfloat *data,
                  guint  n_fan_dets,
                  guint  n_rings);

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
    UfoRofexProcessDarksTaskPrivate *priv;
    priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE (task);

    copy_gvarray_gint(priv->gv_rings_selection_mask,
                      &priv->rings_selection_mask,
                      &priv->rings_selection_mask_size);

    copy_gvarray_guint(priv->gv_beam_positions,
                       &priv->beam_positions,
                       &priv->n_beam_positions);
}

static void
ufo_rofex_process_darks_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexProcessDarksTaskPrivate *priv;
    priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE (task);

    ufo_buffer_get_requisition(inputs[0], requisition);

    // Change the dimensionality
    requisition->n_dims = 2;
    requisition->dims[1] = priv->n_rings;
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
    gfloat *darks, *avg_darks;
    guint n_fan_dets, n_fan_proj, n_fan_sinos, n_trans_per_portion;

    GValue *gv_portion;
    guint portion;

    priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    n_fan_dets = req.dims[0];
    n_fan_proj = req.dims[1];
    n_fan_sinos = req.dims[2];

    darks = ufo_buffer_get_host_array(inputs[0], NULL);
    avg_darks = ufo_buffer_get_host_array(output, NULL);
    memset(avg_darks, 0, ufo_buffer_get_size(output));

    // Get portion ID.
    gv_portion = ufo_buffer_get_metadata(inputs[0], "portion");
    portion = gv_portion ? g_value_get_uint (gv_portion) : 0;

    // Compute the number of beam transitions per data portion.
    n_trans_per_portion = n_fan_sinos / priv->rings_selection_mask_size;

    // Run computing.
    average_darks (darks,
                   avg_darks,
                   portion,
                   n_trans_per_portion,
                   n_fan_dets,
                   n_fan_proj,
                   n_fan_sinos,
                   priv->n_rings,
                   priv->rings_selection_mask,
                   priv->rings_selection_mask_size,
                   priv->beam_positions,
                   priv->n_beam_positions);

    interp_avg_darks (avg_darks,
                      n_fan_dets,
                      priv->n_rings);

    return TRUE;
}

static void
ufo_rofex_process_darks_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexProcessDarksTaskPrivate *priv;
    GValueArray *array;

    priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
            priv->n_rings = g_value_get_uint(value);
            break;
        case PROP_BEAM_POSITIONS:
            array = (GValueArray *) g_value_get_boxed (value);
            g_value_array_free (priv->gv_beam_positions);
            priv->gv_beam_positions = g_value_array_copy (array);
            break;
        case PROP_RINGS_SELECTION_MASK:
            array = (GValueArray *) g_value_get_boxed (value);
            g_value_array_free (priv->gv_rings_selection_mask);
            priv->gv_rings_selection_mask = g_value_array_copy (array);
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
        case PROP_N_RINGS:
            g_value_set_uint (value, priv->n_rings);
            break;
        case PROP_BEAM_POSITIONS:
            g_value_set_boxed (value, priv->gv_beam_positions);
            break;
        case PROP_RINGS_SELECTION_MASK:
            g_value_set_boxed (value, priv->gv_rings_selection_mask);
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

    GParamSpec *rings_selection_mask_vals =
        g_param_spec_int ("rings-selection-mask-values",
                          "Values of the rings selection mask.",
                          "Elements in rings selection mask",
                          G_MININT,
                          G_MAXINT,
                          (gint) 0,
                          G_PARAM_READWRITE);

    GParamSpec *beam_positions_vals =
        g_param_spec_uint ("beam-positions-values",
                           "Values of the beam positions.",
                           "Elements in beam positions",
                           0,
                           G_MAXUINT,
                           (guint) 0,
                           G_PARAM_READWRITE);

    properties[PROP_N_RINGS] =
        g_param_spec_uint ("number-of-rings",
                           "The number of rings.",
                           "The number of rings.",
                           1, G_MAXUINT, 2,
                           G_PARAM_READWRITE);

    properties[PROP_BEAM_POSITIONS] =
        g_param_spec_value_array ("beam-positions",
                                  "Order in which the beam hits the rings.",
                                  "Order in which the beam hits the rings.",
                                  beam_positions_vals,
                                  G_PARAM_READWRITE);

    properties[PROP_RINGS_SELECTION_MASK] =
        g_param_spec_value_array ("rings-selection-mask",
                                  "Offsets to the affected rings around the ring hitted by the beam.",
                                  "Offsets to the affected rings around the ring hitted by the beam.",
                                  rings_selection_mask_vals,
                                  G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexProcessDarksTaskPrivate));
}

static void
ufo_rofex_process_darks_task_init(UfoRofexProcessDarksTask *self)
{
    self->priv = UFO_ROFEX_PROCESS_DARKS_TASK_GET_PRIVATE(self);
    self->priv->n_rings = 2;

    set_default_rings_selection_mask (&self->priv->gv_rings_selection_mask);
    set_default_beam_positions (&self->priv->gv_beam_positions);
}

void
average_darks (gfloat *darks,
               gfloat *avg,
               guint portion,
               guint n_trans_per_portion,
               guint n_fan_dets,
               guint n_fan_proj,
               guint n_fan_sinos,
               guint n_rings,
               gint  *rings_selection_mask,
               guint rings_selection_mask_size,
               guint *beam_positions,
               guint n_beam_positions)
{
    guint trans_local, trans_global, sino_offset, idx_sino, idx_in, idx_out;
    gint beam_position, ring;
    gfloat factor = 1.0 / (gfloat) (n_fan_sinos * n_fan_proj);

    for (trans_local = 0; trans_local < n_trans_per_portion; trans_local++) {
        trans_global = portion * n_trans_per_portion + trans_local;
        beam_position = beam_positions[trans_global % n_beam_positions];

        for (guint i = 0; i < rings_selection_mask_size; ++i) {
            ring = beam_position + rings_selection_mask[i];
            if (ring < 0 || ring >= (gint) n_rings) {
                continue;
            }

            // Sum the sinograms per ring.
            idx_sino = trans_local * rings_selection_mask_size + i;
            sino_offset = idx_sino * (n_fan_proj * n_fan_dets);

            for (guint proj = 0; proj < n_fan_proj; proj++) {
                for (guint det = 0; det < n_fan_dets; det++) {
                    idx_in = sino_offset + (det + proj * n_fan_dets);
                    idx_out = det + ring * n_fan_dets;
                    avg[idx_out] += darks[idx_in] * factor;
                }
            }
        }
    }
}

// Interplation
void
interp_avg_darks (gfloat *data,
                  guint  n_fan_dets,
                  guint  n_rings)
{
    gfloat val = 0.0;
    guint idx_left, idx_right, idx_sino_val;
    for (guint ring = 0; ring < n_rings; ring++) {
        for (guint det = 0; det < n_fan_dets; det++) {
            idx_sino_val = ring * n_fan_dets + det;
            if (data[idx_sino_val] > 300) {
                idx_right = ring * n_fan_dets + (det + 1) % n_fan_dets;
                idx_left = ring * n_fan_dets + (det - 1) % n_fan_dets;

                val = data[idx_left] + data[idx_right];
                data[idx_sino_val] = 0.5 * val;
            }
        }
    }
}
