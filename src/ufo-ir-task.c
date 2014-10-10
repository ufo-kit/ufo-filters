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
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library. If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-ir-task.h"
#include <ufo/ufo.h>
#include <ufo/ir/ufo-ir.h>

static void ufo_task_interface_init (UfoTaskIface *iface);
G_DEFINE_TYPE_WITH_CODE (UfoIrTask, ufo_ir_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_IR_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_IR_TASK, UfoIrTaskPrivate))

struct _UfoIrTaskPrivate {
    UfoPluginManager *plugin_manager;
    UfoResources     *resources;
    cl_command_queue cmd_queue;

    UfoIrMethod         *method;
    UfoIrGeometry       *geometry;
    UfoIrProjector      *projector;
    UfoIrPriorKnowledge *prior;
};

enum {
    PROP_0,
    PROP_METHOD,
    PROP_GEOMETRY,
    PROP_PROJECTOR,
    PROP_PRIOR_KNOWLEDGE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = {NULL, };

UfoNode *
ufo_ir_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_IR_TASK, NULL));
}

static void
ufo_ir_task_setup (UfoTask      *task,
                   UfoResources *resources,
                   GError       **error)
{
    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (task);
    UfoGpuNode *node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));

    priv->resources = g_object_ref (resources);
    priv->cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    UFO_RESOURCES_CHECK_CLERR (clRetainCommandQueue (priv->cmd_queue));

    UfoProfiler  *profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    ufo_ir_geometry_setup  (priv->geometry, priv->resources, error);
    g_object_set (priv->projector,
                  "ufo-profiler", profiler,
                  "geometry", priv->geometry,
                  "command-queue", priv->cmd_queue, NULL);

    ufo_processor_setup (UFO_PROCESSOR (priv->projector),
                            priv->resources,
                            error);

    if (error && *error)
      return;

    g_object_set (priv->method,
                  "projection-model", priv->projector,
                  "command-queue", priv->cmd_queue,
                  "ufo-profiler", profiler, NULL);
    if (priv->prior) {
      g_object_set (priv->method, "prior-knowledge", priv->prior, NULL);
    }
    ufo_processor_setup (UFO_PROCESSOR (priv->method),
                         priv->resources,
                         error);
}

UfoNode *
ufo_ir_task_node_copy (UfoNode *node,
                       GError **error)
{
    UfoNode *copy = UFO_NODE (ufo_ir_task_new());
    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (node);

    UfoIrMethod    *copy_method = ufo_copyable_copy (priv->method, NULL);
    if (copy_method) {
        g_object_set (G_OBJECT (copy), "method", copy_method, NULL);
        g_object_unref (copy_method);
    }

    UfoIrProjector *copy_projector =  ufo_copyable_copy (priv->projector, NULL);
    if (copy_projector) {
        g_object_set (G_OBJECT (copy), "projector", copy_projector, NULL);
        g_object_unref (copy_projector);
    }

    UfoIrGeometry  *copy_geometry =  ufo_copyable_copy (priv->geometry, NULL);
    if (copy_geometry) {
        g_object_set (G_OBJECT (copy), "geometry", copy_geometry, NULL);
        g_object_unref (copy_geometry);
    }

    UfoIrPriorKnowledge  *copy_prior =  ufo_ir_prior_knowledge_copy (priv->prior);
    if (copy_prior) {
        g_object_set (G_OBJECT (copy), "prior-knowledge", copy_prior, NULL);
        ufo_ir_prior_knowledge_unref (copy_prior);
    }

    return copy;
}

static void
ufo_ir_task_get_requisition (UfoTask        *task,
                             UfoBuffer      **inputs,
                             UfoRequisition *requisition)
{
    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (task);

    UfoRequisition in_req;
    ufo_buffer_get_requisition (inputs[0], &in_req);

    GError *error = NULL;
    ufo_ir_geometry_configure (priv->geometry, &in_req, &error);
    if (error) {
      g_error ("Error: %s", error->message);
      g_error_free (error);
    }
    ufo_ir_geometry_get_volume_requisitions (priv->geometry,
                                          requisition);
    ufo_processor_configure (UFO_PROCESSOR (priv->projector));
}


static guint
ufo_ir_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_ir_task_get_num_dimensions (UfoTask *task,
                                guint   input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_ir_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}


static gboolean
ufo_ir_task_process (UfoTask        *task,
                     UfoBuffer      **inputs,
                     UfoBuffer      *output,
                     UfoRequisition *requisition)
{
    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (task);
    ufo_op_set (output, 0, priv->resources, priv->cmd_queue);
    gboolean res = ufo_method_process (UFO_METHOD (priv->method), inputs[0], output, NULL);
    return TRUE;
}

static void
ufo_task_set_json_object_property_real (UfoTask     *task,
                                        const gchar *prop_name,
                                        JsonObject  *json_obj)
{

    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (task);
    gpointer obj = NULL;

    if (g_strcmp0 (prop_name, "geometry") == 0) {
        obj = ufo_ir_geometry_from_json (json_obj, priv->plugin_manager);
    }
    else if (g_strcmp0 (prop_name, "projector") == 0) {
        obj = ufo_ir_projector_from_json (json_obj, priv->plugin_manager);
    }
    else if (g_strcmp0 (prop_name, "method") == 0) {
        obj = ufo_object_from_json (json_obj, priv->plugin_manager);
    }
    else if (g_strcmp0 (prop_name, "prior-knowledge") == 0) {
        obj = ufo_ir_prior_knowledge_from_json (json_obj, priv->plugin_manager);
    }

    g_object_set (task, prop_name, obj, NULL);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_ir_task_setup;
    iface->get_num_inputs = ufo_ir_task_get_num_inputs;
    iface->get_num_dimensions = ufo_ir_task_get_num_dimensions;
    iface->get_mode = ufo_ir_task_get_mode;
    iface->get_requisition = ufo_ir_task_get_requisition;
    iface->process = ufo_ir_task_process;
    iface->set_json_object_property = ufo_task_set_json_object_property_real;
}

static void
ufo_ir_task_set_property (GObject      *object,
                          guint        property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (object);

    switch (property_id) {
      case PROP_METHOD:
          priv->method = g_object_ref (g_value_get_pointer(value));
          break;
      case PROP_GEOMETRY:
          priv->geometry = g_object_ref (g_value_get_pointer(value));
          break;
      case PROP_PROJECTOR:
          priv->projector = g_object_ref (g_value_get_pointer(value));
          break;
      case PROP_PRIOR_KNOWLEDGE:
          priv->prior = g_hash_table_ref (g_value_get_pointer(value));
          break;
      default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
          break;
    }
}

static void
ufo_ir_task_get_property (GObject    *object,
                          guint      property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (object);

    switch (property_id) {
      case PROP_METHOD:
          g_value_set_pointer (value, priv->method);
          break;
      case PROP_GEOMETRY:
          g_value_set_pointer (value, priv->geometry);
          break;
      case PROP_PROJECTOR:
          g_value_set_pointer (value, priv->projector);
          break;
      case PROP_PRIOR_KNOWLEDGE:
          g_value_set_pointer (value, priv->prior);
          break;
      default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
          break;
    }
}

static void
ufo_ir_task_dispose (GObject *object)
{
    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (object);

    g_clear_object (&priv->resources);
    g_clear_object (&priv->method);
    g_clear_object (&priv->geometry);
    g_clear_object (&priv->projector);

    if (priv->prior) {
        g_hash_table_unref (priv->prior);
        priv->prior = NULL;
    }

    G_OBJECT_CLASS (ufo_ir_task_parent_class)->dispose (object);
}

static void
ufo_ir_task_finalize (GObject *object)
{
    UfoIrTaskPrivate *priv = UFO_IR_TASK_GET_PRIVATE (object);
    if (priv->cmd_queue)
      UFO_RESOURCES_CHECK_CLERR (clReleaseCommandQueue(priv->cmd_queue));

    G_OBJECT_CLASS (ufo_ir_task_parent_class)->finalize (object);
}

static void
ufo_ir_task_class_init (UfoIrTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->set_property = ufo_ir_task_set_property;
    gobject_class->get_property = ufo_ir_task_get_property;
    gobject_class->finalize = ufo_ir_task_finalize;
    gobject_class->dispose = ufo_ir_task_dispose;

    properties[PROP_METHOD] =
        g_param_spec_pointer("method",
                             "Pointer to the instance of UfoIrMethod.",
                             "Pointer to the instance of UfoIrMethod.",
                             G_PARAM_READWRITE);

    properties[PROP_GEOMETRY] =
        g_param_spec_pointer("geometry",
                             "Pointer to the instance of UfoGeometry.",
                             "Pointer to the instance of UfoGeometry.",
                             G_PARAM_READWRITE);

    properties[PROP_PROJECTOR] =
        g_param_spec_pointer("projector",
                             "Pointer to the instance of UfoProjector.",
                             "Pointer to the instance of UfoProjector.",
                             G_PARAM_READWRITE);

    properties[PROP_PRIOR_KNOWLEDGE] =
        g_param_spec_pointer("prior-knowledge",
                             "Pointer to the instance of UfoPriorKnowledge.",
                             "Pointer to the instance of UfoPriorKnowledge.",
                             G_PARAM_READWRITE);

      guint i;
      for (i = PROP_0 + 1; i < N_PROPERTIES; i++)
          g_object_class_install_property (gobject_class, i, properties[i]);

      g_type_class_add_private (gobject_class, sizeof(UfoIrTaskPrivate));

      UFO_NODE_CLASS (klass)->copy = ufo_ir_task_node_copy;
}

static void
ufo_ir_task_init(UfoIrTask *self)
{
    UfoIrTaskPrivate *priv = NULL;
    self->priv = priv = UFO_IR_TASK_GET_PRIVATE(self);
    priv->plugin_manager = ufo_plugin_manager_new (NULL);
    priv->resources = NULL;
    priv->cmd_queue = NULL;

    priv->method = NULL;
    priv->geometry = NULL;
    priv->projector = NULL;
    priv->prior = NULL;
}
