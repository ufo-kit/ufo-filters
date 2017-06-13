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
#include "rofex.h"
#include "ufo-rofex-process-flats-task.h"


/*
  DESCRIPTION:
  The filter finds an averaged sinogram for each ring using measurements along
  the transitions.

  INPUT:
  A stack of 2D images:
    0: nModsPerRing * nDetsPerModule
    1: nFanProjections
    2: nTransPerPortion * ringsSelectionMaskSize

  OUTPUT:
  A 2D image:
    0: nModsPerRing * nDetsPerModule
    1: nFanProjections * nRings
*/

struct _UfoRofexProcessFlatsTaskPrivate {
    guint  n_rings;
    gfloat threshold_min;
    gfloat threshold_max;

    GValueArray *gv_beam_positions;
    GValueArray *gv_rings_selection_mask;

    gint *rings_selection_mask;
    guint rings_selection_mask_size;
    guint *beam_positions;
    guint n_beam_positions;
    gfloat *filter;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexProcessFlatsTask, ufo_rofex_process_flats_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_PROCESS_FLATS_TASK, UfoRofexProcessFlatsTaskPrivate))

enum {
    PROP_0,
    PROP_N_RINGS,
    PROP_THRESHOLD_MIN,
    PROP_THRESHOLD_MAX,
    PROP_BEAM_POSITIONS,
    PROP_RINGS_SELECTION_MASK,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

// Correction
static gfloat FILTER_FUNCTION[17]= {
  0.5, 1.0, 1.0, 1.0, 1.5, 2.0, 3.0, 3.5, 2.0, 3.5, 3.0, 2.0, 1.5, 1.0, 1.0, 1.0, 0.5
};

void
correct_flats(gfloat *ref_values,
              gfloat *filter_function,
              gfloat threshold_min,
              gfloat threshold_max,
              guint  n_fan_dets,
              guint  n_fan_proj,
              guint  n_fan_sinos);
void
find_defect_detectors (gfloat *ref_values,
                       gfloat *filter_function,
                       guint  *defect_detectors,
                       gfloat threshold_min,
                       gfloat threshold_max,
                       guint  n_fan_dets,
                       guint  n_fan_proj);

void
interpolate_defect_detectors (gfloat *ref_values,
                              guint  *defect_detectors,
                              guint  n_fan_dets,
                              guint  n_fan_proj);

// Averaging
void
average_flats (gfloat *flats,
               gfloat *avg_flats,
               guint  portion,
               guint  n_trans_per_portion,
               guint  n_fan_dets,
               guint  n_fan_proj,
               guint  n_fan_sinos,
               guint  n_rings,
               gint   *rings_selection_mask,
               guint  rings_selection_mask_size,
               guint  *beam_positions,
               guint  n_beam_positions);

UfoNode *
ufo_rofex_process_flats_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_PROCESS_FLATS_TASK, NULL));
}

static void
ufo_rofex_process_flats_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexProcessFlatsTaskPrivate *priv;
    priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE (task);

    // Compute the filter
    priv->filter = (gfloat*) g_malloc(sizeof(FILTER_FUNCTION));
    guint filter_size = sizeof(FILTER_FUNCTION)/ sizeof(gfloat);

    gfloat sum = 0.0;
    for (guint i = 0; i < filter_size; ++i) {
        sum += FILTER_FUNCTION[i];
    }

    for (guint i = 0; i < filter_size; ++i) {
        priv->filter[i] = FILTER_FUNCTION[i] / sum;
    }

    copy_gvarray_gint(priv->gv_rings_selection_mask,
                      &priv->rings_selection_mask,
                      &priv->rings_selection_mask_size);

    copy_gvarray_guint(priv->gv_beam_positions,
                       &priv->beam_positions,
                       &priv->n_beam_positions);
}

static void
ufo_rofex_process_flats_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexProcessFlatsTaskPrivate *priv;
    UfoRequisition req;
    guint n_fan_dets, n_fan_proj;

    priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    n_fan_dets = req.dims[0];
    n_fan_proj = req.dims[1];

    requisition->n_dims = 2;
    requisition->dims[0] = n_fan_dets;
    requisition->dims[1] = n_fan_proj * priv->n_rings;
}

static guint
ufo_rofex_process_flats_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_process_flats_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_process_flats_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_process_flats_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexProcessFlatsTaskPrivate *priv;

    UfoRequisition req;
    gfloat *flats, *avg_flats;
    guint n_fan_dets, n_fan_proj, n_fan_sinos, n_trans_per_portion;

    GValue *gv_portion;
    guint portion;

    priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    n_fan_dets = req.dims[0];
    n_fan_proj = req.dims[1];
    n_fan_sinos = req.dims[2];

    flats = ufo_buffer_get_host_array(inputs[0], NULL);
    avg_flats = ufo_buffer_get_host_array(output, NULL);
    memset(avg_flats, 0, ufo_buffer_get_size(output));

    correct_flats(flats,
                  priv->filter,
                  priv->threshold_min,
                  priv->threshold_max,
                  n_fan_dets,
                  n_fan_proj,
                  n_fan_sinos);

    // Get portion ID.
    gv_portion = ufo_buffer_get_metadata(inputs[0], "portion");
    portion = gv_portion ? g_value_get_uint (gv_portion) : 0;

    // Compute the number of beam transitions per data portion.
    n_trans_per_portion = n_fan_sinos / priv->rings_selection_mask_size;

    // Run computing.
    average_flats(flats,
                  avg_flats,
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

    return TRUE;
}

static void
ufo_rofex_process_flats_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  UfoRofexProcessFlatsTaskPrivate *priv;
  GValueArray *array;

  priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
            priv->n_rings = g_value_get_uint(value);
            break;
        case PROP_THRESHOLD_MIN:
            priv->threshold_min = g_value_get_float(value);
            break;
        case PROP_THRESHOLD_MAX:
            priv->threshold_max = g_value_get_float(value);
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
ufo_rofex_process_flats_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexProcessFlatsTaskPrivate *priv;
    priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
            g_value_set_uint (value, priv->n_rings);
            break;
        case PROP_THRESHOLD_MIN:
            g_value_set_float(value, priv->threshold_min);
            break;
        case PROP_THRESHOLD_MAX:
            g_value_set_float(value, priv->threshold_max);
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
ufo_rofex_process_flats_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_process_flats_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_process_flats_task_setup;
    iface->get_num_inputs = ufo_rofex_process_flats_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_process_flats_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_process_flats_task_get_mode;
    iface->get_requisition = ufo_rofex_process_flats_task_get_requisition;
    iface->process = ufo_rofex_process_flats_task_process;
}

static void
ufo_rofex_process_flats_task_class_init (UfoRofexProcessFlatsTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_process_flats_task_set_property;
    oclass->get_property = ufo_rofex_process_flats_task_get_property;
    oclass->finalize = ufo_rofex_process_flats_task_finalize;

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

    properties[PROP_THRESHOLD_MIN] =
        g_param_spec_float ("threshold-min",
                            "The minimum of threshold range.",
                            "The minimum of threshold range.",
                            G_MINFLOAT, G_MAXFLOAT, 0.67,
                            G_PARAM_READWRITE);

    properties[PROP_THRESHOLD_MAX] =
        g_param_spec_float ("threshold-max",
                            "The maximum of threshold range.",
                            "The maximum of threshold range.",
                            G_MINFLOAT, G_MAXFLOAT, 1.5,
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

    g_type_class_add_private (oclass, sizeof(UfoRofexProcessFlatsTaskPrivate));
}

static void
ufo_rofex_process_flats_task_init(UfoRofexProcessFlatsTask *self)
{
    self->priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE(self);
    self->priv->n_rings = 2;
    self->priv->threshold_min = 0.67;
    self->priv->threshold_max = 1.5;

    set_default_rings_selection_mask (&self->priv->gv_rings_selection_mask);
    set_default_beam_positions (&self->priv->gv_beam_positions);
}

void
average_flats (gfloat *flats,
               gfloat *avg_flats,
               guint  portion,
               guint  n_trans_per_portion,
               guint  n_fan_dets,
               guint  n_fan_proj,
               guint  n_fan_sinos,
               guint  n_rings,
               gint   *rings_selection_mask,
               guint  rings_selection_mask_size,
               guint  *beam_positions,
               guint  n_beam_positions)
{
    guint trans_local, trans_global, sino_offset;
    guint idx_sino, idx_in, idx_out;
    gint beam_position, ring;
    guint n_sino_vals;
    guint *rings_hits;

    rings_hits = g_malloc0(n_rings * sizeof(guint));
    n_sino_vals = n_fan_dets * n_fan_proj;

    for (trans_local = 0; trans_local < n_trans_per_portion; trans_local++) {
        trans_global = portion * n_trans_per_portion + trans_local;
        beam_position = beam_positions[trans_global % n_beam_positions];

        for (guint i = 0; i < rings_selection_mask_size; ++i) {
            ring = beam_position + rings_selection_mask[i];
            if (ring < 0 || ring >= (gint) n_rings) {
                continue;
            }
            rings_hits[ring] += 1;

            // Sum the sinograms per ring.
            idx_sino = trans_local * rings_selection_mask_size + i;
            sino_offset = idx_sino * (n_fan_proj * n_fan_dets);

            for (guint i = 0; i < n_sino_vals; i++) {
                idx_in = sino_offset + i;
                idx_out = ring * n_sino_vals + i;

                avg_flats[idx_out] += flats[idx_in];
            }
        }
    }

    // Compute average
    for (guint ring = 0; ring < n_rings; ring++){
        for (guint i = 0; i < n_sino_vals; i++) {
            idx_out = ring * n_sino_vals + i;
            avg_flats[idx_out] /= rings_hits[ring];
        }
    }

    g_free (rings_hits);
}

void
correct_flats(gfloat *flats,
              gfloat *filter_function,
              gfloat threshold_min,
              gfloat threshold_max,
              guint  n_fan_dets,
              guint  n_fan_proj,
              guint  n_fan_sinos)
{
    guint *defect_detectors;
    guint n_sino_vals;

    n_sino_vals = n_fan_dets * n_fan_proj;

    for (guint i = 0; i < n_fan_sinos; i++) {
        defect_detectors = g_malloc0 (n_sino_vals * sizeof(guint));
        find_defect_detectors (flats + i * n_sino_vals,
                               filter_function,
                               defect_detectors,
                               threshold_min,
                               threshold_max,
                               n_fan_dets,
                               n_fan_proj);

        interpolate_defect_detectors (flats + i * n_sino_vals,
                                      defect_detectors,
                                      n_fan_dets,
                                      n_fan_proj);

        g_free(defect_detectors);
    }
}

void
find_defect_detectors (gfloat *flats,
                       gfloat *filter_function,
                       guint  *defect_detectors,
                       gfloat threshold_min,
                       gfloat threshold_max,
                       guint  n_fan_dets,
                       guint  n_fan_proj)
{
    gfloat *det_vals;
    guint scale;
    gfloat val_cur, val_next, val_max, val_min;
    guint idx_cur, idx_next;
    gint idx_tmp, n_fan_dets_2;
    gfloat threshold;
    gint wsize;

    det_vals = g_malloc0(n_fan_dets * sizeof(gfloat));

    scale = 2;
    val_cur = 0;
    val_next = 0;

    for (guint det = 0; det < n_fan_dets; det++)
    {
        val_max = flats[det];
        val_min = val_max;

        for (guint proj = 0; proj < (n_fan_proj - 1); proj++)
        {
            idx_cur = det + proj * n_fan_dets;
            idx_next = det + (proj + 1) * n_fan_dets;

            val_cur = flats[idx_cur];
            val_next = flats[idx_next];
            det_vals[det] += abs(val_cur - val_next);

            val_max = MAX(val_cur, val_max);
            val_min = MIN(val_cur, val_min);
        }

        det_vals[det] *= pow(val_max - val_min, scale);
    }

    n_fan_dets_2 = (gint) n_fan_dets / 2;
    wsize = 2;

    for (guint det_seg = 0; det_seg < 2; det_seg++) {
        for (gint i = 0; i < n_fan_dets_2; i++)
        {
            threshold = 0.0;
            for (gint j = 0; j < 9; j++)
            {
                idx_tmp = (i - j) % n_fan_dets_2;
                // In Python: -1 % 10 = 9     5 % 9 = 4
                // In C:      -1 % 10 = -1    5 % 9 = 4
                idx_tmp = (idx_tmp < 0) ? n_fan_dets_2 + idx_tmp : idx_tmp;
                idx_cur = idx_tmp + det_seg * n_fan_dets_2;
                threshold += filter_function[j] * det_vals[idx_cur];

                idx_tmp = (i + j) % n_fan_dets_2;
                idx_cur = idx_tmp + det_seg * n_fan_dets_2;
                threshold += filter_function[j] * det_vals[idx_cur];
            }

            idx_cur = det_seg * n_fan_dets_2 + i;
            if (det_vals[idx_cur] < threshold_min * threshold) {
                defect_detectors[idx_cur] = 1;
            }

            if (det_vals[idx_cur] > threshold_max * threshold) {
                for (int offset = -wsize; offset <= wsize; offset++)
                {
                    defect_detectors[(idx_cur + offset) % n_fan_dets] = 1;
                }
            }
        }
    }
    g_free(det_vals);
}

void
interpolate_defect_detectors (gfloat *flats,
                              guint  *defect_detectors,
                              guint  n_fan_dets,
                              guint  n_fan_proj)
{
    gint idx_tmp, det_a, det_b;
    gfloat val_right, val_left, w1, w0;

    det_a = 0;
    det_b = 0;
    idx_tmp = 0;
    val_right = 0.0;
    val_left = 0.0;

    while (det_b < (gint) n_fan_dets) {
        if (defect_detectors[det_b])
        {
            det_a = det_b;
            while (defect_detectors[det_b] &&
                   defect_detectors[(det_b + 1) % n_fan_dets])
            {
                det_b++;
            }

            for (gint det = det_a; det <= det_b; det++)
            {
                w1 = ((gfloat)det - (gfloat)det_a + 1.0) /
                     ((gfloat)det_b - (gfloat)det_a + 2.0);

                w0 = 1.0 - w1;

                for (guint proj = 0; proj < n_fan_proj; proj++)
                {
                    idx_tmp = (det_a - 1) % n_fan_dets + proj * n_fan_dets;
                    val_left = flats[idx_tmp];

                    idx_tmp = (det_b + 1) % n_fan_dets + proj * n_fan_dets;
                    val_right = flats[idx_tmp];

                    idx_tmp = det % n_fan_dets + proj * n_fan_dets;
                    flats[idx_tmp] = w0 * val_left + w1 * val_right;
                }
            }
        }
        det_b++;
    }
}
