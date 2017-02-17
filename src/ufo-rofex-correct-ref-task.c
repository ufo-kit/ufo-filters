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
#include "ufo-rofex-correct-ref-task.h"


struct _UfoRofexCorrectRefTaskPrivate {
    guint  n_planes;
    gfloat threshold_min;
    gfloat threshold_max;
    gfloat *filter_function;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexCorrectRefTask, ufo_rofex_correct_ref_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_CORRECT_REF_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_CORRECT_REF_TASK, UfoRofexCorrectRefTaskPrivate))

enum {
    PROP_0,
    PROP_N_PLANES,
    PROP_THRESHOLD_MIN,
    PROP_THRESHOLD_MAX,
    N_PROPERTIES
};

static gfloat FILTER_FUNCTION[17]= {
  0.5, 1.0, 1.0, 1.0, 1.5, 2.0, 3.0, 3.5, 2.0, 3.5, 3.0, 2.0, 1.5, 1.0, 1.0, 1.0, 0.5
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_correct_ref_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_CORRECT_REF_TASK, NULL));
}

static void
ufo_rofex_correct_ref_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexCorrectRefTaskPrivate *priv;
    priv = UFO_ROFEX_CORRECT_REF_TASK_GET_PRIVATE (task);

    priv->filter_function = (gfloat*) g_malloc(sizeof(FILTER_FUNCTION));
    guint func_len = sizeof(FILTER_FUNCTION)/ sizeof(gfloat);

    gfloat sum = 0.0;
    for (guint i = 0; i < func_len; ++i) {
        sum += priv->filter_function[i];
    }

    for (guint i = 0; i < func_len; ++i) {
        priv->filter_function[i] = priv->filter_function[i] / sum;
    }

}

static void
ufo_rofex_correct_ref_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
  ufo_buffer_get_requisition(inputs[0], requisition);
}

static guint
ufo_rofex_correct_ref_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_correct_ref_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_correct_ref_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_CPU;
}

void find_defect_detectors (gfloat *ref_values,
                            gfloat *filter_function,
                            guint  *defect_detectors,
                            guint  n_dets,
                            guint  n_proj,
                            gfloat threshold_min,
                            gfloat threshold_max)
{
    gfloat *det_vals = g_malloc0(n_dets * sizeof(gfloat));
    guint scale = 2;
    gfloat cur_val = 0;
    gfloat next_val = 0;
    for (guint detInd = 0; detInd < n_dets; detInd++)
    {
        gfloat valMax = ref_values[detInd];
        gfloat valMin = valMax;

        for (guint projInd = 0; projInd < n_proj; projInd++)
        {
          cur_val  = ref_values[detInd + projInd * n_dets];
          next_val = ref_values[detInd + (projInd + 1) * n_dets];
          det_vals[detInd] += abs(cur_val - next_val);

          if (ref_values[detInd + projInd * n_dets] > valMax)
              valMax = ref_values[detInd + projInd * n_dets];

          if (ref_values[detInd + projInd * n_dets] < valMin)
              valMin = ref_values[detInd + projInd * n_dets];
        }

        det_vals[detInd] *= pow(valMax - valMin, scale);
    }

    int add_neighbour = 2; // add_neighbour_to_flickering
    for (guint detSeg = 0; detSeg < 2; detSeg++) {
      for (guint i = 0; i < n_dets / 2; i++)
      {
          gfloat thresh_segment = 0.0;
          for (guint j = 0; j < 9; j++)
          {
              int ind = (i - j) % (n_dets / 2);
              thresh_segment += filter_function[j] * det_vals[ind + detSeg * (n_dets / 2)];
              ind = (i + j) % (n_dets / 2);
              thresh_segment += filter_function[j] * det_vals[ind + detSeg * (n_dets / 2)];
          }

          guint detInd = detSeg * (n_dets / 2) + i;
          if (det_vals[detInd] < threshold_min * thresh_segment) {
            defect_detectors[detInd] = 1;
          }
          if (det_vals[detInd] > threshold_max * thresh_segment) {
            for (int offset = -add_neighbour; offset <= add_neighbour; offset++)
            {
                defect_detectors[(detInd + offset) % n_dets] = 1;
            }
          }
      }
    }

    g_free(det_vals);
}

void interpolate_defect_detectors (gfloat *ref_values,
                                   guint  *defect_detectors,
                                   guint  n_dets,
                                   guint  n_proj)
{
    guint detInd = 0;
    gfloat lval = 0.0;
    gfloat rval = 0.0;

    while (detInd < n_dets) {
        if (defect_detectors[detInd]){
            guint det0 = detInd;

            while(defect_detectors[detInd] &&
                  defect_detectors[(detInd + 1) % n_dets])
            {
                detInd++;
            }

            for (guint i = det0; i <= detInd; i++)
            {
                gfloat w1 = ((gfloat)i - (gfloat)det0 + 1.0) /
                            ((gfloat)detInd - (gfloat)det0 + 2.0);
                gfloat w0 = 1.0 - w1;

                for (guint projInd = 0; projInd < n_proj; projInd++)
                {
                  lval = ref_values[(det0 - 1)%n_dets + projInd * n_dets];
                  rval = ref_values[(det0 + 1)%n_dets + projInd * n_dets];

                  ref_values[(i%n_dets + projInd*n_dets)] = w0 * lval + w1 * rval;
                }
            }
        }
        detInd++;
    }
}

static gboolean
ufo_rofex_correct_ref_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexCorrectRefTaskPrivate *priv;
    priv = UFO_ROFEX_CORRECT_REF_TASK_GET_PRIVATE (task);

    UfoRequisition in_req;
    ufo_buffer_get_requisition(inputs[0], &in_req);

    guint n_dets = in_req.dims[0];
    guint n_proj = in_req.dims[1];
    guint n_vals = in_req.dims[2];

    ufo_buffer_copy(inputs[0], output);
    gfloat *h_ref_values = ufo_buffer_get_host_array(output, NULL);

    for (guint i = 0; i < n_vals; i++) {
        guint *defect_detectors = g_malloc0(n_proj * n_dets * sizeof(guint));

        find_defect_detectors (h_ref_values + i * n_dets * n_proj,
                               priv->filter_function,
                               defect_detectors,
                               n_dets,
                               n_proj,
                               priv->threshold_min,
                               priv->threshold_max);

        interpolate_defect_detectors (h_ref_values + i * n_dets * n_proj,
                                      defect_detectors,
                                      n_dets,
                                      n_proj);
        g_free(defect_detectors);
    }

    return TRUE;
}


static void
ufo_rofex_correct_ref_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexCorrectRefTaskPrivate *priv;
    priv = UFO_ROFEX_CORRECT_REF_TASK_GET_PRIVATE (object);

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
ufo_rofex_correct_ref_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexCorrectRefTaskPrivate *priv = UFO_ROFEX_CORRECT_REF_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            g_value_set_uint(value, priv->n_planes);
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
ufo_rofex_correct_ref_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_correct_ref_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_correct_ref_task_setup;
    iface->get_num_inputs = ufo_rofex_correct_ref_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_correct_ref_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_correct_ref_task_get_mode;
    iface->get_requisition = ufo_rofex_correct_ref_task_get_requisition;
    iface->process = ufo_rofex_correct_ref_task_process;
}

static void
ufo_rofex_correct_ref_task_class_init (UfoRofexCorrectRefTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_correct_ref_task_set_property;
    oclass->get_property = ufo_rofex_correct_ref_task_get_property;
    oclass->finalize = ufo_rofex_correct_ref_task_finalize;

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

    g_type_class_add_private (oclass, sizeof(UfoRofexCorrectRefTaskPrivate));
}

static void
ufo_rofex_correct_ref_task_init(UfoRofexCorrectRefTask *self)
{
    self->priv = UFO_ROFEX_CORRECT_REF_TASK_GET_PRIVATE(self);
    self->priv->n_planes = 1;
    self->priv->threshold_min = 0.67;
    self->priv->threshold_max = 1.5;
}
