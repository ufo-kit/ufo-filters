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
#include <stdio.h>
#include <string.h>
#include "ufo-rofex-process-flats-task.h"

/*
  This filter should be used after reordering stage. It corrects flat field
  images (also so cold 'reference images') and averages them along the frames
  afterwards.

  It is assumed that filter receives a 3D stack of the following dimensions:
    0: nDetsPerModule * nDetModules
    1: nProjections
    2: nPlanes * nFrames

  As result it produces a data set of dimensions:
    0: nDetsPerModule * nDetModules
    1: nProjections * nPlanes
*/

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
              guint  n_planes,
              guint  n_frames);
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
average_flats (gfloat *sinos,
               gfloat *avg,
               guint  n_fan_dets,
               guint  n_fan_proj,
               guint  n_planes,
               guint  n_frames);

struct _UfoRofexProcessFlatsTaskPrivate {
    guint  n_planes;
    gfloat threshold_min;
    gfloat threshold_max;

    gfloat *filter;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexProcessFlatsTask, ufo_rofex_process_flats_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_PROCESS_FLATS_TASK, UfoRofexProcessFlatsTaskPrivate))

enum {
    PROP_0,
    PROP_N_PLANES,
    PROP_THRESHOLD_MIN,
    PROP_THRESHOLD_MAX,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

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
    guint func_len = sizeof(FILTER_FUNCTION)/ sizeof(gfloat);

    gfloat sum = 0.0;
    for (guint i = 0; i < func_len; ++i) {
        sum += FILTER_FUNCTION[i];
    }

    for (guint i = 0; i < func_len; ++i) {
        priv->filter[i] = FILTER_FUNCTION[i] / sum;
    }
}

static void
ufo_rofex_process_flats_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexProcessFlatsTaskPrivate *priv;
    UfoRequisition req;

    priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    guint n_fan_dets = req.dims[0];
    guint n_fan_proj = req.dims[1];

    requisition->n_dims = 2;
    requisition->dims[0] = n_fan_dets;
    requisition->dims[1] = n_fan_proj * priv->n_planes;
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

    priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    guint n_frames = req.dims[2] / priv->n_planes;
    guint n_fan_dets = req.dims[0];
    guint n_fan_proj = req.dims[1];

    gfloat *flats = ufo_buffer_get_host_array(inputs[0], NULL);
    gfloat *avg_flats = ufo_buffer_get_host_array(output, NULL);
    memset(avg_flats, 0, ufo_buffer_get_size(output));

    correct_flats(flats,
                  priv->filter,
                  priv->threshold_min,
                  priv->threshold_max,
                  n_fan_dets,
                  n_fan_proj,
                  priv->n_planes,
                  n_frames);

    average_flats(flats,
                  avg_flats,
                  n_fan_dets,
                  n_fan_proj,
                  priv->n_planes,
                  n_frames);

    return TRUE;
}

static void
ufo_rofex_process_flats_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  UfoRofexProcessFlatsTaskPrivate *priv;
  priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
            break;
        case PROP_THRESHOLD_MIN:
            priv->threshold_min = g_value_get_float(value);
            break;
        case PROP_THRESHOLD_MAX:
            priv->threshold_max = g_value_get_float(value);
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
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
            break;
        case PROP_THRESHOLD_MIN:
            g_value_set_float(value, priv->threshold_min);
            break;
        case PROP_THRESHOLD_MAX:
            g_value_set_float(value, priv->threshold_max);
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

    properties[PROP_N_PLANES] =
                g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
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

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexProcessFlatsTaskPrivate));
}

static void
ufo_rofex_process_flats_task_init(UfoRofexProcessFlatsTask *self)
{
    self->priv = UFO_ROFEX_PROCESS_FLATS_TASK_GET_PRIVATE(self);
    self->priv->n_planes = 1;
    self->priv->threshold_min = 0.67;
    self->priv->threshold_max = 1.5;
}

void
correct_flats(gfloat *flats,
              gfloat *filter_function,
              gfloat threshold_min,
              gfloat threshold_max,
              guint  n_fan_dets,
              guint  n_fan_proj,
              guint  n_planes,
              guint  n_frames)
{
    guint *defect_detectors;


    guint n_images = n_planes * n_frames;
    for (guint i = 0; i < n_images; i++) {
      defect_detectors = g_malloc0(n_fan_dets * n_fan_proj * sizeof(guint));

      find_defect_detectors (flats + i * n_fan_dets * n_fan_proj,
                            filter_function,
                            defect_detectors,
                            threshold_min,
                            threshold_max,
                            n_fan_dets,
                            n_fan_proj);

      interpolate_defect_detectors (
                            flats + i * n_fan_dets * n_fan_proj,
                            defect_detectors,
                            n_fan_dets,
                            n_fan_proj);

      g_free(defect_detectors);
    }
}

void
average_flats (gfloat *sinos,
               gfloat *avg,
               guint  n_fan_dets,
               guint  n_fan_proj,
               guint  n_planes,
               guint  n_frames)
{
    gfloat factor = 1.0 / (gfloat) (n_frames);
    guint framesize = n_fan_dets * n_fan_proj;

    gfloat val;
    guint idx;
    for (guint frame = 0; frame < n_frames; frame++) {
        for (guint plane = 0; plane < n_planes; plane++) {
          for (guint i = 0; i < framesize; i++) {
              idx = i + (frame + plane) * framesize;
              val = sinos[idx];

              idx = i + plane * framesize;
              avg[idx] += val * factor;
          }
        }
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
    gfloat *det_vals = g_malloc0(n_fan_dets * sizeof(gfloat));

    guint scale = 2;
    gfloat cur_val = 0;
    gfloat nxt_val = 0;

    guint cur_idx, nxt_idx;
    for (guint det = 0; det < n_fan_dets; det++)
    {
        gfloat max_val = flats[det];
        gfloat min_val = max_val;

        for (guint proj = 0; proj < n_fan_proj - 1; proj++)
        {
            cur_idx = det + proj * n_fan_dets;
            nxt_idx = det + (proj + 1) * n_fan_dets;

            cur_val = flats[cur_idx];
            nxt_val = flats[nxt_idx];
            det_vals[det] += abs(cur_val - nxt_val);

            if (flats[cur_idx] > max_val)
                max_val = flats[cur_idx];

            if (flats[cur_idx] < min_val)
                min_val = flats[cur_idx];
        }

        det_vals[det] *= pow(max_val - min_val, scale);
    }

    gfloat threshold_segment;
    gint half_fan_dets = (gint)n_fan_dets / 2;
    gint wsize = 2; // add_neighbour_to_flickering
    gint idx;

    for (guint det_seg = 0; det_seg < 2; det_seg++) {
        for (gint i = 0; i < half_fan_dets; i++)
        {
            threshold_segment = 0.0;
            for (gint j = 0; j < 9; j++)
            {
                idx = (i - j) % half_fan_dets;
                // In Python: -1 % 10 = 9     4 % 9 = 4
                // In C:      -1 % 10 = -1    5 % 9 = 4
                idx = (idx < 0) ? half_fan_dets + idx : idx;
                cur_idx = idx + det_seg * half_fan_dets;
                threshold_segment += filter_function[j] * det_vals[cur_idx];

                idx = (i + j) % half_fan_dets;
                cur_idx = idx + det_seg * half_fan_dets;
                threshold_segment += filter_function[j] * det_vals[cur_idx];
            }

            guint det = det_seg * half_fan_dets + i;
            if (det_vals[det] < threshold_min * threshold_segment) {
                defect_detectors[det] = 1;
            }

            if (det_vals[det] > threshold_max * threshold_segment) {
                for (int offset = -wsize; offset <= wsize; offset++)
                {
                    defect_detectors[(det + offset) % n_fan_dets] = 1;
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
    guint detA = 0, detB = 0;
    guint idx = 0;
    gfloat lval = 0.0;
    gfloat rval = 0.0;

    while (detB < n_fan_dets) {
        if (defect_detectors[detB])
        {
            detA = detB;
            while (defect_detectors[detB] &&
                   defect_detectors[(detB + 1) % n_fan_dets])
            {
                detB++;
            }

            for (gint i = detA; i <= detB; i++)
            {
                gfloat w1 = ((gfloat)i - (gfloat)detA + 1.0) /
                            ((gfloat)detB - (gfloat)detA + 2.0);

                gfloat w0 = 1.0 - w1;

                for (gint proj = 0; proj < n_fan_proj; proj++)
                {
                    idx = (detA - 1) % n_fan_dets + proj * n_fan_dets;
                    lval = flats[idx];

                    idx = (detB + 1) % n_fan_dets + proj * n_fan_dets;
                    rval = flats[idx];

                    idx = i % n_fan_dets + proj * n_fan_dets;
                    flats[idx] = w0 * lval + w1 * rval;
                }
            }
        }
        detB++;
    }
}
