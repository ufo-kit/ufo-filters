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


#include "ufo-rofex-data-aggregator-task.h"

/*
  Description:
  The filter aggregate data from the detector modules and puts them in a way
  that all measurements related to a (plane, frame) are grouped in 2D image.
  These 2D images are stacked in a 3D image, which later can be send by
  chunks of slices or complete.

  Input:
  A series of 2D images, where each image represents a data collected by
  a single detector module. Each image should have the following dimensions:
    0: nDetsPerModule * nProjections
    1: nPlanes * nFrames

  Output:
  A 3D image of the following dimensions:
    0: nDetsPerModule * nProjections
    1: nDetModules
    2: portionSize
*/


struct _UfoRofexDataAggregatorTaskPrivate {
    guint    module;
    guint    portion;
    guint    n_portions;
    gboolean generated;
    gsize    data_size;
    gfloat   *data;

    guint n_modules;
    guint n_planes;
    guint portion_size;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexDataAggregatorTask, ufo_rofex_data_aggregator_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_DATA_AGGREGATOR_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_DATA_AGGREGATOR_TASK, UfoRofexDataAggregatorTaskPrivate))

enum {
    PROP_0,
    PROP_N_MODULES,
    PROP_N_PLANES,
    PROP_PORTION_SIZE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_data_aggregator_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_DATA_AGGREGATOR_TASK, NULL));
}

static void
ufo_rofex_data_aggregator_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexDataAggregatorTaskPrivate *priv;
    priv = UFO_ROFEX_DATA_AGGREGATOR_TASK_GET_PRIVATE (task);

    if (priv->portion_size != 1 && priv->portion_size % priv->n_planes) {
        g_error("The portion size must be a multiple of the number of planes.");
    }

    priv->module = 0;
    priv->portion = 0;
    priv->n_portions = 0;
    priv->generated = FALSE;
    priv->data_size = 0;
    priv->data = NULL;
}

static void
ufo_rofex_data_aggregator_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexDataAggregatorTaskPrivate *priv;
    UfoRequisition req;

    priv = UFO_ROFEX_DATA_AGGREGATOR_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    if (priv->portion_size > req.dims[1]) {
        g_warning ("The portion size is too large for supplied data. \
        It will be changed to the maximum for provided data: %lu", req.dims[1]);
        priv->portion_size = req.dims[1];
    }

    priv->n_portions = (guint) ceil ((gfloat)req.dims[1] / priv->portion_size);

    requisition->dims[0] = req.dims[0];
    requisition->dims[1] = priv->n_modules;
    requisition->dims[2] = priv->portion_size;
    requisition->n_dims = 3;
}

static guint
ufo_rofex_data_aggregator_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_data_aggregator_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_rofex_data_aggregator_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_data_aggregator_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexDataAggregatorTaskPrivate *priv;
    UfoRequisition req;
    guint out_offset, in_offset;
    gsize imgsize, rowsize;
    gfloat *in_mem;

    priv = UFO_ROFEX_DATA_AGGREGATOR_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &req);

    in_mem = (gfloat *) ufo_buffer_get_host_array (inputs[0], NULL);
    imgsize = requisition->dims[0] * requisition->dims[1];
    rowsize = req.dims[0] * sizeof(gfloat);

    if (!priv->data) {
        priv->data_size = ufo_buffer_get_size (inputs[0]) * priv->n_modules;
        priv->data = (gfloat *) g_malloc0 (priv->data_size);
    }

    for (guint i = 0; i < req.dims[1]; i++) {
        in_offset = i * req.dims[0];
        out_offset = priv->module * req.dims[0] + i * imgsize;
        memcpy (priv->data + out_offset, in_mem + in_offset, rowsize);
    }

    priv->module++;
    if (priv->module == priv->n_modules) {
        priv->module = 0;
        priv->generated = FALSE;
        return FALSE;
    }

    return TRUE;
}

static gboolean
ufo_rofex_data_aggregator_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexDataAggregatorTaskPrivate *priv;
    priv = UFO_ROFEX_DATA_AGGREGATOR_TASK_GET_PRIVATE (task);

    if (!priv->generated) {
        gfloat *out_mem;
        gsize  out_size, chunk_size;
        guint  out_len;
        gsize  offset;

        out_mem = (gfloat *) ufo_buffer_get_host_array (output, NULL);
        out_size = ufo_buffer_get_size (output);
        out_len = out_size / sizeof(gfloat);
        offset = priv->portion * out_len;

        chunk_size = priv->data_size - priv->portion * out_size;
        chunk_size = chunk_size > out_size ? out_size : chunk_size;

        memset (out_mem, 0, out_size);
        memcpy (out_mem, priv->data + offset, chunk_size);

        priv->portion++;
        if (priv->portion_size == 1) {
            // Plane index should be signed, since -1 means no plane-index
            gint plane_index = priv->portion % priv->n_planes;
            GValue gv_plane_index = {0};
            g_value_init (&gv_plane_index, G_TYPE_UINT);
            g_value_set_uint (&gv_plane_index, plane_index);
            ufo_buffer_set_metadata (output, "plane-index", &gv_plane_index);
        }

        if (priv->portion >= priv->n_portions) {
            priv->generated = TRUE;
            priv->portion = 0;
        }

        return TRUE;
    }

    return FALSE;
}

static void
ufo_rofex_data_aggregator_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexDataAggregatorTaskPrivate *priv;
    priv = UFO_ROFEX_DATA_AGGREGATOR_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_MODULES:
            priv->n_modules = g_value_get_uint (value);
            break;
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint (value);
            break;
        case PROP_PORTION_SIZE:
            priv->portion_size = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_data_aggregator_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexDataAggregatorTaskPrivate *priv;
    priv = UFO_ROFEX_DATA_AGGREGATOR_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_MODULES:
            g_value_set_uint (value, priv->n_modules);
            break;
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
            break;
        case PROP_PORTION_SIZE:
            g_value_set_uint (value, priv->portion_size);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_data_aggregator_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_data_aggregator_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_data_aggregator_task_setup;
    iface->get_num_inputs = ufo_rofex_data_aggregator_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_data_aggregator_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_data_aggregator_task_get_mode;
    iface->get_requisition = ufo_rofex_data_aggregator_task_get_requisition;
    iface->process = ufo_rofex_data_aggregator_task_process;
    iface->generate = ufo_rofex_data_aggregator_task_generate;
}

static void
ufo_rofex_data_aggregator_task_class_init (UfoRofexDataAggregatorTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_data_aggregator_task_set_property;
    oclass->get_property = ufo_rofex_data_aggregator_task_get_property;
    oclass->finalize = ufo_rofex_data_aggregator_task_finalize;

    properties[PROP_N_MODULES] =
            g_param_spec_uint ("number-of-modules",
                               "The number of detector modules",
                               "The number of detector modules",
                               1, G_MAXUINT, 1,
                               G_PARAM_READWRITE);

    properties[PROP_N_PLANES] =
            g_param_spec_uint ("number-of-planes",
                               "The number of planes",
                               "The number of planes",
                               1, G_MAXUINT, 1,
                               G_PARAM_READWRITE);

    properties[PROP_PORTION_SIZE] =
            g_param_spec_uint ("portion-size",
                               "The number of (planes x frames) pushed forward.",
                               "The number of (planes x frames) pushed forward.",
                               1, G_MAXUINT, 1,
                               G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexDataAggregatorTaskPrivate));
}

static void
ufo_rofex_data_aggregator_task_init(UfoRofexDataAggregatorTask *self)
{
    self->priv = UFO_ROFEX_DATA_AGGREGATOR_TASK_GET_PRIVATE(self);
    self->priv->n_modules = 1;
    self->priv->n_planes = 1;
    self->priv->portion_size = 1;
}
