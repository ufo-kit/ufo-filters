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

#include <glib.h>
#include <math.h>
#include <string.h>
#include "ufo-rofex-aggregate-task.h"


/*
  DESCRIPTION:
  The filter aggregates the data collected by all detector modules during
  a number of the beam transitions between the rings. The filter allows
  you to split the data into portions, which size is determined by
  the number of transitions.

  INPUT:
  A stack of 1D images:
    0: nDetsPerModule * nProjections * nTransPerPortion

  OUTPUT:
  A series of 3D images:
    0: nDetsPerModule * nProjections
    1: nTransPerPortion
    2: nModulePairs
*/


struct _UfoRofexAggregateTaskPrivate {
    guint n_trans_per_portion;
    guint max_portions;
    guint n_rings;
    guint n_mods_per_ring;
    guint n_dets_per_module;
    guint n_fan_proj;

    gfloat *data;
    guint  n_modpairs;
    guint  n_modpairs_processed;

    gboolean generated;
    gboolean global_stop;
    guint n_trans_local;
    guint n_portions_local;
    guint portion_local;
    guint portion_global;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexAggregateTask, ufo_rofex_aggregate_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_AGGREGATE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_AGGREGATE_TASK, UfoRofexAggregateTaskPrivate))

enum {
    PROP_0,
    PROP_N_TRANS_PER_PORTION,
    PROP_MAX_PORTIONS,
    PROP_N_RINGS,
    PROP_N_MODS_PER_RING,
    PROP_N_DETS_PER_MODULE,
    PROP_N_FAN_PROJ,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_aggregate_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_AGGREGATE_TASK, NULL));
}

static void
ufo_rofex_aggregate_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexAggregateTaskPrivate *priv;
    priv = UFO_ROFEX_AGGREGATE_TASK_GET_PRIVATE (task);

    priv->data = NULL;
    priv->n_modpairs = 0;
    priv->n_modpairs_processed = 0;

    priv->generated = TRUE;
    priv->global_stop = FALSE;
    priv->n_trans_local = 0;
    priv->n_portions_local = 0;
    priv->portion_local = 0;
    priv->portion_global = 0;
}

static void
ufo_rofex_aggregate_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexAggregateTaskPrivate *priv;
    guint n_vals_measured, n_vals_per_trans, n_modpairs_per_ring;

    priv = UFO_ROFEX_AGGREGATE_TASK_GET_PRIVATE (task);

    n_vals_measured = ufo_buffer_get_size (inputs[0]) / sizeof(gfloat);
    n_vals_per_trans = priv->n_dets_per_module * priv->n_fan_proj;

    // Determine the number of portions
    priv->n_portions_local = 1;
    priv->n_trans_local = ceil (n_vals_measured / n_vals_per_trans);
    if (priv->n_trans_local > priv->n_trans_per_portion) {
        // We have more than one portion
        priv->n_portions_local = ceil ((gfloat)priv->n_trans_local / priv->n_trans_per_portion);
    }

    // Compute the output requisitions
    n_modpairs_per_ring =  priv->n_mods_per_ring / 2.0;
    priv->n_modpairs = priv->n_rings * n_modpairs_per_ring;

    requisition->dims[0] = n_vals_per_trans;
    requisition->dims[1] = priv->n_trans_per_portion;
    requisition->dims[2] = priv->n_modpairs;
    requisition->n_dims = 3;
}

static guint
ufo_rofex_aggregate_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_aggregate_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 1;
}

static UfoTaskMode
ufo_rofex_aggregate_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_aggregate_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexAggregateTaskPrivate *priv;
    gfloat *chunk;
    gsize n_bytes;
    guint offset;

    priv = UFO_ROFEX_AGGREGATE_TASK_GET_PRIVATE (task);

    if (priv->global_stop) {
        // Stop if we have processed enough portions.
        // Works only if the limitation is set.
        priv->generated = TRUE;
        return FALSE;
    }

    if (!priv->data) {
        n_bytes = ufo_buffer_get_size (inputs[0]) * priv->n_modpairs;
        priv->data = (gfloat *) g_malloc0 (n_bytes);
    }

    // Copy data chunk to data buffer.
    n_bytes = ufo_buffer_get_size (inputs[0]);
    chunk = (gfloat *) ufo_buffer_get_host_array (inputs[0], NULL);

    offset = priv->n_modpairs_processed * (n_bytes / sizeof (gfloat));
    memcpy (priv->data + offset, chunk, n_bytes);
    priv->n_modpairs_processed++;

    if (priv->n_modpairs_processed == priv->n_modpairs) {
        // Be sure that number of files match the expected
        // amount of pairs of modules.

        priv->n_modpairs_processed = 0;
        priv->generated = FALSE;
        return FALSE;
    }

    return TRUE;
}

static gboolean
ufo_rofex_aggregate_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexAggregateTaskPrivate *priv;
    priv = UFO_ROFEX_AGGREGATE_TASK_GET_PRIVATE (task);
    if (!priv->generated && !priv->global_stop) {
        gfloat *mem_out;
        gsize n_bytes;
        guint n_vals_per_trans, n_vals_per_modpair_in, n_vals_per_modpair_out;
        guint offset_portion, offset_in, offset_out;
        gboolean local_stop;

        //
        // It is required to set the portion index.
        GValue gv_portion = {0};
        g_value_init (&gv_portion, G_TYPE_UINT);
        g_value_set_uint (&gv_portion, priv->portion_global);
        ufo_buffer_set_metadata (output, "portion", &gv_portion);

        //
        // Copy data related to this portion.
        n_bytes = ufo_buffer_get_size (output);
        mem_out = (gfloat *) ufo_buffer_get_host_array (output, NULL);
        memset (mem_out, 0, n_bytes);

        n_vals_per_trans = priv->n_dets_per_module * priv->n_fan_proj;
        n_vals_per_modpair_in = n_vals_per_trans * priv->n_trans_local;
        n_vals_per_modpair_out = n_vals_per_trans * priv->n_trans_per_portion;
        offset_portion = n_vals_per_modpair_out * priv->portion_local;

        n_bytes = n_vals_per_modpair_out * sizeof (gfloat);
        for (guint modpair = 0; modpair < priv->n_modpairs; modpair++) {
            offset_in  = modpair * n_vals_per_modpair_in + offset_portion;
            offset_out = modpair * n_vals_per_modpair_out;
            memcpy (mem_out + offset_out, priv->data + offset_in, n_bytes);
        }

        priv->portion_local++;
        priv->portion_global++;

        local_stop = (priv->portion_local >= priv->n_portions_local);
        priv->global_stop = (priv->portion_global >= priv->max_portions &&
                             priv->max_portions != 0);

        if (local_stop || priv->global_stop) {
            priv->generated = TRUE;
            priv->portion_local = 0;
        }

        return TRUE;
    }

    return FALSE;
}


static void
ufo_rofex_aggregate_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexAggregateTaskPrivate *priv;
    priv = UFO_ROFEX_AGGREGATE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_TRANS_PER_PORTION:
            priv->n_trans_per_portion = g_value_get_uint (value);
            break;
        case PROP_MAX_PORTIONS:
            priv->max_portions = g_value_get_uint (value);
            break;
        case PROP_N_RINGS:
            priv->n_rings = g_value_get_uint (value);
            break;
        case PROP_N_MODS_PER_RING:
            priv->n_mods_per_ring = g_value_get_uint (value);
            break;
        case PROP_N_DETS_PER_MODULE:
            priv->n_dets_per_module = g_value_get_uint (value);
            break;
        case PROP_N_FAN_PROJ:
            priv->n_fan_proj = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_aggregate_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexAggregateTaskPrivate *priv;
    priv = UFO_ROFEX_AGGREGATE_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_TRANS_PER_PORTION:
            g_value_set_uint (value, priv->n_trans_per_portion);
            break;
        case PROP_MAX_PORTIONS:
            g_value_set_uint (value, priv->max_portions);
            break;
        case PROP_N_RINGS:
            g_value_set_uint (value, priv->n_rings);
            break;
        case PROP_N_MODS_PER_RING:
            g_value_set_uint (value, priv->n_mods_per_ring);
            break;
        case PROP_N_DETS_PER_MODULE:
            g_value_set_uint (value, priv->n_dets_per_module);
            break;
        case PROP_N_FAN_PROJ:
            g_value_set_uint (value, priv->n_fan_proj);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_aggregate_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_aggregate_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_aggregate_task_setup;
    iface->get_num_inputs = ufo_rofex_aggregate_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_aggregate_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_aggregate_task_get_mode;
    iface->get_requisition = ufo_rofex_aggregate_task_get_requisition;
    iface->process = ufo_rofex_aggregate_task_process;
    iface->generate = ufo_rofex_aggregate_task_generate;
}

static void
ufo_rofex_aggregate_task_class_init (UfoRofexAggregateTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_aggregate_task_set_property;
    oclass->get_property = ufo_rofex_aggregate_task_get_property;
    oclass->finalize = ufo_rofex_aggregate_task_finalize;

    properties[PROP_N_TRANS_PER_PORTION] =
        g_param_spec_uint ("number-of-transitions-per-portion",
                            "The number of beam transitions per data portion.",
                            "The number of beam transitions per data portion.",
                            1, G_MAXUINT, 1,
                            G_PARAM_READWRITE);

    properties[PROP_MAX_PORTIONS] =
        g_param_spec_uint ("max-portions",
                           "The number of portions to be produced. Zero value means no limitations.",
                           "The number of portions to be produced. Zero value means no limitations.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE);

    properties[PROP_N_RINGS] =
        g_param_spec_uint ("number-of-rings",
                           "The number of rings.",
                           "The number of rings. ",
                           1, G_MAXUINT, 2,
                           G_PARAM_READWRITE);

    properties[PROP_N_MODS_PER_RING] =
        g_param_spec_uint ("number-of-modules-per-ring",
                           "The number of modules per ring.",
                           "The number of modules per ring.",
                           1, G_MAXUINT, 18,
                           G_PARAM_READWRITE);

    properties[PROP_N_DETS_PER_MODULE] =
        g_param_spec_uint ("number-of-detectors-per-module",
                           "The number of detectors per module.",
                           "The number of detectors per module.",
                           1, G_MAXUINT, 16,
                           G_PARAM_READWRITE);

    properties[PROP_N_FAN_PROJ] =
        g_param_spec_uint ("number-of-fan-projections",
                           "The number of fan projections.",
                           "The number of fan projections.",
                           1, G_MAXUINT, 1,
                           G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexAggregateTaskPrivate));
}

static void
ufo_rofex_aggregate_task_init(UfoRofexAggregateTask *self)
{
    self->priv = UFO_ROFEX_AGGREGATE_TASK_GET_PRIVATE(self);
    self->priv->n_trans_per_portion = 1;
    self->priv->max_portions = 0;
    self->priv->n_rings = 2;
    self->priv->n_mods_per_ring = 18;
    self->priv->n_dets_per_module = 16;
    self->priv->n_fan_proj = 1;
}
