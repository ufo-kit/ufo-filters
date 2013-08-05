/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
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

#include <ufo/ufoart.h>
#include "ufo-art-task.h"

/**
 * SECTION:ufo-art-task
 * @Title: art
 *
 */

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoArtTask, ufo_art_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_ART_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ART_TASK, UfoArtTaskPrivate))

struct _UfoArtTaskPrivate {
    gchar *method_key;
    gchar *projector_key;
    gchar *regularizer_key;
    gfloat angle_step;
    guint max_iterations;
    gboolean posc_enabled;
    guint max_regularizer_iterations;

    UfoART *method;
    UfoProjector *projector;
    UfoRegularizer *regularizer;

    UfoGeometry geometry;
    gfloat *angles;

    UfoResources *resources;
    cl_command_queue command_queue;
};

enum {
    PROP_0,
    PROP_METHOD,
    PROP_PROJECTOR,
    PROP_ANGLE_STEP,
    PROP_MAX_ITERATIONS,
    PROP_POSC,
    PROP_REGULARIZER,
    PROP_MAX_REGULARIZER_ITERATIONS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_art_task_new (void)
{ PRINT_METHOD
  return UFO_NODE (g_object_new (UFO_TYPE_ART_TASK, NULL));
}

static void
ufo_art_task_setup (UfoTask *task,
                    UfoResources *resources,
                    GError **error)
{ PRINT_METHOD

  UfoArtTaskPrivate *priv = UFO_ART_TASK_GET_PRIVATE (task);
  priv->resources = g_object_ref(resources);
  GList *cmd_queues = ufo_resources_get_cmd_queues (priv->resources);
  guint task_id =  ufo_node_get_index(UFO_NODE(task));
  priv->command_queue = g_list_nth_data (cmd_queues, task_id);
  clRetainCommandQueue (priv->command_queue);
 
  // TODO: use plugin manager and try to get appropriate projector and rest
  // using keys.
  UfoPluginManager *plugin_manager = ufo_plugin_manager_new (NULL);
  GError *_error = NULL;

  if (!_error && priv->method_key)
    priv->method = ufo_plugin_manager_get_art(plugin_manager, priv->method_key, &_error);
  
  if (!_error && priv->projector_key)
    priv->projector = ufo_plugin_manager_get_projector(plugin_manager, priv->projector_key, &_error);
 
  if (!_error && priv->regularizer_key)
    priv->regularizer = ufo_plugin_manager_get_regularizer(plugin_manager, priv->regularizer_key, &_error);
  
  if (_error) goto setup_error;

  if (UFO_IS_ART(priv->method)) {
    ufo_art_set_resources (priv->method, priv->resources);
    ufo_art_map_command_queues (priv->method, task_id);
    g_object_set(priv->method, "posc", priv->posc_enabled, NULL);
  }

  if (UFO_IS_PROJECTOR(priv->projector)) {
    ufo_projector_set_resources (priv->projector, priv->resources);
    ufo_projector_initialize (priv->projector, &_error);
    if (_error) goto setup_error;

    ufo_art_set_projector (priv->method, priv->projector);
  }
  
  if (UFO_IS_REGULARIZER(priv->regularizer)) {
    ufo_regularizer_set_resources (priv->regularizer, priv->resources);
    ufo_regularizer_initialize (priv->regularizer, &_error);
    if (_error) goto setup_error;

    ufo_art_set_regularizer (priv->method, priv->regularizer);
    g_object_set(priv->regularizer, "max-iterations", priv->max_regularizer_iterations, NULL);
  }
  
  return;

  setup_error:
    g_error("%s", _error->message);
    g_propagate_error (error, _error);
    return;
}

static void
ufo_art_task_get_requisition (UfoTask *task,
                              UfoBuffer **inputs,
                              UfoRequisition *requisition)
{ PRINT_METHOD
  UfoArtTaskPrivate *priv = UFO_ART_TASK_GET_PRIVATE (task);
  UfoBuffer *sinogram = inputs[0];
  UfoRequisition sino_requisitions;
  ufo_buffer_get_requisition (sinogram, &sino_requisitions);

  gsize n_angles = sino_requisitions.dims[1];
  gsize n_detectors = sino_requisitions.dims[0];

  if (priv->angles == NULL || 
      n_angles != priv->geometry.proj_angles) {
    // reallocate memory for angles
    if (priv->angles) g_free (priv->angles);
    priv->angles = g_malloc0 (sizeof(gfloat) * n_angles);

    // set angles according to the angle step
    guint i = 0;
    priv->angles[i] = 0;
    for (i = 1; i < n_angles; ++i) {
      priv->angles[i] = priv->angles[i-1] + priv->angle_step;
    }
  }

  priv->geometry.vol_width   = (unsigned int)n_detectors;
  priv->geometry.vol_height  = (unsigned int)n_detectors;
  priv->geometry.proj_dets   = (unsigned int)n_detectors;
  priv->geometry.proj_angles = (unsigned int)n_angles;
  priv->geometry.det_scale   = 1.0f;

  ufo_art_set_geometry (priv->method, &priv->geometry, priv->angles);

  requisition->n_dims = 2;
  requisition->dims[0] = priv->geometry.vol_width;
  requisition->dims[1] = priv->geometry.vol_height;
}

static void
ufo_art_task_get_structure (UfoTask *task,
                            guint *n_inputs,
                            UfoInputParam **in_params,
                            UfoTaskMode *mode)
{ PRINT_METHOD
  *mode = UFO_TASK_MODE_PROCESSOR;
  *n_inputs = 1;
  *in_params = g_new0 (UfoInputParam, 1);
  (*in_params)[0].n_dims = 2;
}

static gboolean
ufo_art_task_process (UfoGpuTask     *task,
                      UfoBuffer     **inputs,
                      UfoBuffer      *output,
                      UfoRequisition *requisition)
{ PRINT_METHOD
  #warning "For what the reason the requisition needs here?"

  GError *error = NULL;
  UfoArtTaskPrivate *priv = UFO_ART_TASK_GET_PRIVATE (task);

  UfoBuffer *sino = inputs[0];
  UfoBuffer *volume = output;

// TODO: instead of op_set we should copy initial assumption into the volume
  ufo_op_set (volume, 0, priv->resources, priv->command_queue);

  g_print ("START: %s (%s projector, %d max iterations)",
    priv->method_key, priv->projector_key, priv->max_iterations);

  if (priv->regularizer) {
    g_print(" + %s (%d max iterations)",
      priv->regularizer_key, priv->max_regularizer_iterations);
  }

  ufo_art_iterate (priv->method, volume, sino, priv->max_iterations, &error);
  if (error){
    g_error (error->message);
    return FALSE;
  }

  return TRUE;  
}

static void
ufo_art_task_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{ PRINT_METHOD
  UfoArtTaskPrivate *priv = UFO_ART_TASK_GET_PRIVATE (object);

  switch (property_id) {
    case PROP_METHOD:
         g_free (priv->method_key);
         priv->method_key = g_strdup(g_value_get_string (value));
         break;
    case PROP_PROJECTOR:
         g_free (priv->projector_key);
         priv->projector_key = g_strdup(g_value_get_string (value));
         break;
    case PROP_ANGLE_STEP:
         priv->angle_step = g_value_get_float (value);
         break;
    case PROP_MAX_ITERATIONS:
         priv->max_iterations = g_value_get_uint (value);
         break;
    case PROP_POSC:
          priv->posc_enabled = g_value_get_boolean (value);
        break;
    case PROP_REGULARIZER:
         g_free (priv->regularizer_key);
         priv->regularizer_key = g_strdup(g_value_get_string (value));
         break;
    case PROP_MAX_REGULARIZER_ITERATIONS:
         priv->max_regularizer_iterations = g_value_get_uint (value);
         break;
    default:
         G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void
ufo_art_task_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{ PRINT_METHOD
  UfoArtTaskPrivate *priv = UFO_ART_TASK_GET_PRIVATE (object);

  switch (property_id) {
    case PROP_METHOD:
        g_value_set_string (value, priv->method_key);
        break;
    case PROP_PROJECTOR:
        g_value_set_string (value, priv->projector_key);
        break;
    case PROP_ANGLE_STEP:
        g_value_set_float (value, priv->angle_step);
        break;
    case PROP_MAX_ITERATIONS:
        g_value_set_uint (value, priv->max_iterations);
        break;
    case PROP_POSC:
        g_value_set_boolean(value, priv->posc_enabled);
        break;
    case PROP_REGULARIZER:
        g_value_set_string (value, priv->regularizer_key);
        break;
    case PROP_MAX_REGULARIZER_ITERATIONS:
        g_value_set_uint (value, priv->max_regularizer_iterations);
        break;    
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}

static void
ufo_art_task_dispose (GObject *object)
{
    UfoArtTaskPrivate *priv = UFO_ART_TASK_GET_PRIVATE (object);

    if (priv->method != NULL) {
        g_object_unref (priv->method);
        priv->method = NULL;
    }

    if (priv->projector != NULL) {
        g_object_unref (priv->projector);
        priv->projector = NULL;
    }

    if (priv->regularizer != NULL) {
        g_object_unref (priv->regularizer);
        priv->regularizer = NULL;
    }

    if (priv->resources != NULL) {
        g_object_unref (priv->resources);
        priv->resources = NULL;
    }

    G_OBJECT_CLASS (ufo_art_task_parent_class)->dispose (object);
}

static void
ufo_art_task_finalize (GObject *object)
{
  UfoArtTaskPrivate *priv = UFO_ART_TASK_GET_PRIVATE (object);
  
  if (priv->method_key) g_free (priv->method_key);
  if (priv->projector_key)  g_free (priv->projector_key);
  if (priv->regularizer_key) g_free (priv->regularizer_key);

  g_free (priv->angles);
  UFO_CHECK_CLERR (clReleaseCommandQueue(priv->command_queue));
  G_OBJECT_CLASS (ufo_art_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{ PRINT_METHOD
  iface->setup = ufo_art_task_setup;
  iface->get_structure = ufo_art_task_get_structure;
  iface->get_requisition = ufo_art_task_get_requisition;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{ PRINT_METHOD
  iface->process = ufo_art_task_process;
}

static void
ufo_art_task_class_init (UfoArtTaskClass *klass)
{ PRINT_METHOD
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  const gfloat limit = (gfloat) (4.0 * G_PI);

  gobject_class->set_property = ufo_art_task_set_property;
  gobject_class->get_property = ufo_art_task_get_property;
  gobject_class->finalize = ufo_art_task_finalize;
  gobject_class->dispose = ufo_art_task_dispose;

  properties[PROP_METHOD] =
      g_param_spec_string ("method",
            "ART method title",
            "The property describing the ART method to use.",
            "sart",
            G_PARAM_READWRITE);

    properties[PROP_PROJECTOR] =
        g_param_spec_string ("projector",
            "Projector title",
            "The property describing the projector to use.",
            "default",
            G_PARAM_READWRITE);

    properties[PROP_ANGLE_STEP] =
        g_param_spec_float ("angle-step",
                            "Increment of angle in radians",
                            "Increment of angle in radians",
                            -limit, +limit, 0.0f,
                            G_PARAM_READWRITE);

    properties[PROP_MAX_ITERATIONS] =
    g_param_spec_uint ("max-iterations",
                        "Maximum number of iterations over all data",
                        "Maximum number of iterations over all data",
                        1, G_MAXUINT, 1,
                        G_PARAM_READWRITE);

    properties[PROP_POSC] =
    g_param_spec_boolean ("posc",
                        "Positive constraint",
                        "Positive constraint",
                        FALSE,
                        G_PARAM_READWRITE);
  
    properties[PROP_REGULARIZER] =
        g_param_spec_string ("regularizer",
            "Regularizer title",
            "The property describing the regularizer to use.",
            "",
            G_PARAM_READWRITE);

    properties[PROP_MAX_REGULARIZER_ITERATIONS] =
    g_param_spec_uint ("max-regularizer-iterations",
                        "Maximum number of iterations for regularizer",
                        "Maximum number of iterations for regularizer",
                        1, G_MAXUINT, 20,
                        G_PARAM_READWRITE);

    guint i;
    for (i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoArtTaskPrivate));
}

static void
ufo_art_task_init(UfoArtTask *self)
{ PRINT_METHOD
  UfoArtTaskPrivate *priv = NULL;
  self->priv = priv = UFO_ART_TASK_GET_PRIVATE(self);
  ufo_geometry_reset (&priv->geometry);
  priv->method = NULL;
  priv->projector = NULL;
  priv->regularizer = NULL;
  priv->angles = NULL;
  priv->angle_step = 1;
  priv->max_iterations = 1;
  priv->max_regularizer_iterations = 1;

  priv->projector_key = NULL;
  priv->method_key = NULL;
  priv->regularizer_key = NULL;

  priv->resources = NULL;
  priv->command_queue = NULL;
}
