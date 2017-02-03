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

#include "ufo-rofex-make-sino-task.h"


struct _UfoRofexMakeSinoTaskPrivate {
    guint n_modules;
    guint n_det_per_module;
    guint n_projections;
    guint n_planes;

    guint n_slices;
    guint n_processed;
    gboolean generated;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexMakeSinoTask, ufo_rofex_make_sino_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_MAKE_SINO_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_MAKE_SINO_TASK, UfoRofexMakeSinoTaskPrivate))

enum {
    PROP_0,
    PROP_N_MODULES,
    PROP_N_DET_PER_MODULE,
    PROP_N_PROJECTIONS,
    PROP_N_PLANES,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_make_sino_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_MAKE_SINO_TASK, NULL));
}

static void
ufo_rofex_make_sino_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
  UfoRofexMakeSinoTaskPrivate *priv = UFO_ROFEX_MAKE_SINO_TASK_GET_PRIVATE (task);
  priv->n_slices = 1;
  priv->n_processed = 0;
  priv->generated = FALSE;
}

static void
ufo_rofex_make_sino_task_get_requisition (UfoTask *task,
                                          UfoBuffer **inputs,
                                          UfoRequisition *requisition)
{
    UfoRofexMakeSinoTaskPrivate *priv = UFO_ROFEX_MAKE_SINO_TASK_GET_PRIVATE (task);

    // Find the number of values i
    UfoRequisition in_req;
    ufo_buffer_get_requisition(inputs[0], &in_req);
    guint in_len = 1;
    for (guint i = 0; i < in_req.n_dims; ++i) {
      in_len *= in_req.dims[i];
    }

    // Find the number of slices (i.e. frames)
    guint n_dets = priv->n_det_per_module * priv->n_modules;
    guint n_proj = priv->n_projections;
    priv->n_slices = in_len / (priv->n_det_per_module * n_proj * priv->n_planes);

    // Specify requisition
    requisition->n_dims = 3;
    requisition->dims[0] = n_dets;
    requisition->dims[1] = n_proj;
    requisition->dims[2] = priv->n_slices * priv->n_planes;
}

static guint
ufo_rofex_make_sino_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_make_sino_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_make_sino_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_make_sino_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    /*
      Inputs[0] - a single data buffer which contains values measured for a
        particular detector module. The values are ordered as follows:
        [

          [
            [detPixel1 detPixel2 ... ], // projection 1
            ..................
            [detPixel1 detPixel2 ... ]  // projection N
          ], // plane 1
          ...

          [
            [detPixel1 detPixel2 ... ], // projection 1
            ..................
            [detPixel1 detPixel2 ... ]  // projection N
          ], // plane K

        ] // slice 1
    */

    UfoRofexMakeSinoTaskPrivate *priv = UFO_ROFEX_MAKE_SINO_TASK_GET_PRIVATE (task);
    gfloat *h_output = ufo_buffer_get_host_array(output, NULL);
    gfloat *h_detModValues = ufo_buffer_get_host_array(inputs[0], NULL);

    /*
      Go through the data buffer and copy its values on respective positions
      in the output buffer.
    */

    guint n_dets_per_mod = priv->n_det_per_module;
    guint n_dets   = priv->n_det_per_module * priv->n_modules;
    guint n_proj   = priv->n_projections;
    guint n_planes = priv->n_planes;
    guint n_slices = priv->n_slices;

    guint modInd = 0;
    guint src_offset = 0;
    guint dst_offset = 0;

    for (guint sliceInd = 0; sliceInd < n_slices; sliceInd++) {
        for (guint planeInd = 0; planeInd < n_planes; planeInd++) {
            for (guint projInd = 0; projInd < n_proj; projInd++)
            {
                src_offset = projInd * n_dets_per_mod +
                    (planeInd + sliceInd * n_planes) * n_dets_per_mod * n_proj;

                modInd = priv->n_processed;

                dst_offset = modInd * n_dets_per_mod + projInd * n_dets +
                    (planeInd + sliceInd * n_planes) * n_dets * n_proj;

                memcpy(h_output + dst_offset,
                       h_detModValues + src_offset,
                       n_dets_per_mod * sizeof(gfloat));
            }
        }
    }

    // If we have processed stated number of detector modules, then stop.
    priv->n_processed ++;

    if (priv->n_processed >= priv->n_modules) {
      // Data has been collected and sorted. Ready for sinogram generating.
      priv->n_processed = 0;
      priv->generated = FALSE;
      return FALSE;
    }

    return TRUE;
}

static gboolean
ufo_rofex_make_sino_task_generate (UfoTask *task,
                                   UfoBuffer *output,
                                   UfoRequisition *requisition)
{
    UfoRofexMakeSinoTaskPrivate *priv = UFO_ROFEX_MAKE_SINO_TASK_GET_PRIVATE (task);

    if (!priv->generated) {
      priv->generated = TRUE;
      return TRUE;
    }

    return FALSE;
}

static void
ufo_rofex_make_sino_task_get_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexMakeSinoTaskPrivate *priv = UFO_ROFEX_MAKE_SINO_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_MODULES:
            g_value_set_uint (value, priv->n_modules);
            break;
        case PROP_N_DET_PER_MODULE:
            g_value_set_uint (value, priv->n_det_per_module);
            break;
        case PROP_N_PROJECTIONS:
            g_value_set_uint (value, priv->n_projections);
            break;
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_make_sino_task_set_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexMakeSinoTaskPrivate *priv = UFO_ROFEX_MAKE_SINO_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_MODULES:
            priv->n_modules = g_value_get_uint(value);
            break;
        case PROP_N_DET_PER_MODULE:
            priv->n_det_per_module = g_value_get_uint(value);
            break;
        case PROP_N_PROJECTIONS:
            priv->n_projections = g_value_get_uint(value);
            break;
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_make_sino_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_make_sino_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_make_sino_task_setup;
    iface->get_num_inputs = ufo_rofex_make_sino_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_make_sino_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_make_sino_task_get_mode;
    iface->get_requisition = ufo_rofex_make_sino_task_get_requisition;
    iface->process = ufo_rofex_make_sino_task_process;
    iface->generate = ufo_rofex_make_sino_task_generate;
}

static void
ufo_rofex_make_sino_task_class_init (UfoRofexMakeSinoTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_make_sino_task_set_property;
    oclass->get_property = ufo_rofex_make_sino_task_get_property;
    oclass->finalize = ufo_rofex_make_sino_task_finalize;

    properties[PROP_N_MODULES] =
        g_param_spec_uint ("number-of-modules",
                           "The number of detector modules",
                           "The number of detector modules",
                           1, G_MAXUINT, 27,
                           G_PARAM_READWRITE);

    properties[PROP_N_DET_PER_MODULE] =
               g_param_spec_uint ("number-of-detectors-per-module",
                                  "The number of pixels per detector module",
                                  "The number of pixels per detector module",
                                  1, G_MAXUINT, 16,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PROJECTIONS] =
               g_param_spec_uint ("number-of-projections",
                                  "The number of fan-beam projections",
                                  "The number of fan-beam projections",
                                  1, G_MAXUINT, 180,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PLANES] =
               g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 4,
                                  G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexMakeSinoTaskPrivate));
}

static void
ufo_rofex_make_sino_task_init(UfoRofexMakeSinoTask *self)
{
    self->priv = UFO_ROFEX_MAKE_SINO_TASK_GET_PRIVATE(self);
    self->priv->n_modules = 27;
    self->priv->n_det_per_module = 16;
    self->priv->n_projections = 180;
    self->priv->n_planes = 1;
}
