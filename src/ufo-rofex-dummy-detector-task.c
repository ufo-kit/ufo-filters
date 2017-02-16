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

#include "ufo-rofex-dummy-detector-task.h"


struct _UfoRofexDummyDetectorTaskPrivate {
    guint n_modules;
    guint n_det_per_module;
    guint n_projections;
    guint n_planes;
    guint n_frames;

    guint current_module;
    guint current_plane;
    guint current_frame;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexDummyDetectorTask, ufo_rofex_dummy_detector_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_DUMMY_DETECTOR_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_DUMMY_DETECTOR_TASK, UfoRofexDummyDetectorTaskPrivate))

enum {
  PROP_0,
  PROP_N_MODULES,
  PROP_N_DET_PER_MODULE,
  PROP_N_PROJECTIONS,
  PROP_N_PLANES,
  PROP_N_FRAMES,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_dummy_detector_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_DUMMY_DETECTOR_TASK, NULL));
}

static void
ufo_rofex_dummy_detector_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexDummyDetectorTaskPrivate *priv;
    priv = UFO_ROFEX_DUMMY_DETECTOR_TASK_GET_PRIVATE (task);

    priv->current_module = 1;
    priv->current_plane = 1;
    priv->current_frame = 1;
}

static void
ufo_rofex_dummy_detector_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
  UfoRofexDummyDetectorTaskPrivate *priv;
  priv = UFO_ROFEX_DUMMY_DETECTOR_TASK_GET_PRIVATE (task);

  requisition->n_dims = 2;
  requisition->dims[0] = priv->n_det_per_module;
  requisition->dims[1] = priv->n_projections;
}

static guint
ufo_rofex_dummy_detector_task_get_num_inputs (UfoTask *task)
{
    return 0;
}

static guint
ufo_rofex_dummy_detector_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 0;
}

static UfoTaskMode
ufo_rofex_dummy_detector_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_GENERATOR;
}


static gboolean
ufo_rofex_dummy_detector_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexDummyDetectorTaskPrivate *priv;
    priv = UFO_ROFEX_DUMMY_DETECTOR_TASK_GET_PRIVATE (task);

    if ( priv->current_frame > priv->n_frames ) {
        // all frames were processed.
        // done.
        return FALSE;
    }

    gfloat *data;
    data = ufo_buffer_get_host_array(output, NULL);

    for (guint proj_ind = 0; proj_ind < priv->n_projections; proj_ind++) {
        for (guint det_ind = 0; det_ind < priv->n_det_per_module; det_ind++)
        {
            guint index = det_ind + proj_ind * priv->n_det_per_module;
            data[index] = priv->current_module;
        }
    }

    priv->current_module++;

    if ( priv->current_module > priv->n_modules ) {
      // all modules for the current plane generated a data chunk,
      // now do next plane
      priv->current_module = 1;
      priv->current_plane++;
    }

    if ( priv->current_plane > priv->n_planes ) {
      // all planes for current frame were processed
      // now do next frame
      priv->current_plane = 1;
      priv->current_frame++;
    }

    return TRUE;
}

static void
ufo_rofex_dummy_detector_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexDummyDetectorTaskPrivate *priv = UFO_ROFEX_DUMMY_DETECTOR_TASK_GET_PRIVATE (object);

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
        case PROP_N_FRAMES:
            priv->n_frames = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_dummy_detector_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexDummyDetectorTaskPrivate *priv = UFO_ROFEX_DUMMY_DETECTOR_TASK_GET_PRIVATE (object);

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
        case PROP_N_FRAMES:
            g_value_set_uint (value, priv->n_frames);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_dummy_detector_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_dummy_detector_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_dummy_detector_task_setup;
    iface->get_num_inputs = ufo_rofex_dummy_detector_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_dummy_detector_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_dummy_detector_task_get_mode;
    iface->get_requisition = ufo_rofex_dummy_detector_task_get_requisition;
    iface->generate = ufo_rofex_dummy_detector_task_generate;
}

static void
ufo_rofex_dummy_detector_task_class_init (UfoRofexDummyDetectorTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_dummy_detector_task_set_property;
    oclass->get_property = ufo_rofex_dummy_detector_task_get_property;
    oclass->finalize = ufo_rofex_dummy_detector_task_finalize;

    properties[PROP_N_MODULES] =
        g_param_spec_uint ("number-of-modules",
                           "The number of detector modules",
                           "The number of detector modules",
                           1, G_MAXUINT, 1,
                           G_PARAM_READWRITE);

    properties[PROP_N_DET_PER_MODULE] =
               g_param_spec_uint ("number-of-detectors-per-module",
                                  "The number of pixels per detector module",
                                  "The number of pixels per detector module",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PROJECTIONS] =
               g_param_spec_uint ("number-of-projections",
                                  "The number of fan-beam projections",
                                  "The number of fan-beam projections",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PLANES] =
               g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    properties[PROP_N_FRAMES] =
                g_param_spec_uint ("number-of-frames",
                                   "The number of frames",
                                   "The number of frames made by a detector module",
                                   1, G_MAXUINT, 1,
                                   G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexDummyDetectorTaskPrivate));
}

static void
ufo_rofex_dummy_detector_task_init(UfoRofexDummyDetectorTask *self)
{
    self->priv = UFO_ROFEX_DUMMY_DETECTOR_TASK_GET_PRIVATE(self);
    self->priv->n_modules = 1;
    self->priv->n_det_per_module = 1;
    self->priv->n_projections = 1;
    self->priv->n_planes = 1;
    self->priv->n_frames = 1;
}
