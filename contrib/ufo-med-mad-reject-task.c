/*
 * Replacing pixels by local median value when detected as outliers based on MAD
 * This file is part of ufo-serge filter set.
 * Copyright (C) 2016 Serge Cohen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Serge Cohen <serge.cohen@synchrotron-soleil.fr>
 */
#include "config.h"
#include "ufo-med-mad-reject-task.h"
#include "ufo-sxc-common.h"

#include <stdio.h>

/*
  In general for a type REDUCTOR : process is called to input data and generate to generate the output.
  For the current reject : the first call to process should return TRUE then FALSE (hence we can generate
  the output for first frame).

  Then we should return FALSE to each new call to process (so that we have a chance to generate the filtered
  frame).

  When generate is called WITHOUT a call to process before : it means we are dealing with last frame … act accordingly
*/

struct _UfoMedMadRejectTaskPrivate {
  gfloat threshold;
  cl_kernel kernel;
  UfoBuffer *in0;
  UfoBuffer *in1;
  UfoBuffer *in2;
  gboolean  proc_was_called;
  gboolean  gene_shortcut;
  gint32    proc_call_number;
  gint32    gene_call_number;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoMedMadRejectTask, ufo_med_mad_reject_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_MED_MAD_REJECT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_MED_MAD_REJECT_TASK, UfoMedMadRejectTaskPrivate))

enum {
  PROP_0,
  PROP_THRESHOLD,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_med_mad_reject_task_new (void)
{
  return UFO_NODE (g_object_new (UFO_TYPE_MED_MAD_REJECT_TASK, NULL));
}

// Only called at the start : should be used to setup the reject.
static void
ufo_med_mad_reject_task_setup (UfoTask *task,
                               UfoResources *resources,
                               GError **error)
{
  UfoMedMadRejectTaskPrivate *priv;

  priv = UFO_MED_MAD_REJECT_TASK_GET_PRIVATE (task);

  if ( priv->threshold <= 0.0f ) {
    g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                 "Threshold value %f is not positive",
                 priv->threshold);
    return;
  }

  priv->kernel = ufo_resources_get_kernel (resources, "med-mad-reject.cl", "outliersRej_MedMad_3x3x3_f32", NULL, error);

  if (priv->kernel != NULL)
    UFO_RESOURCES_CHECK_AND_SET (clRetainKernel (priv->kernel), error);
}

// This one is called for each frame, so that it can adapt from one to the other frame
static void
ufo_med_mad_reject_task_get_requisition (UfoTask *task,
                                         UfoBuffer **inputs,
                                         UfoRequisition *requisition,
                                         GError **error)
{
  // Transferring the buffer «size» (size, data type and dimension) to the output
  ufo_buffer_get_requisition(inputs[0], requisition);
}

// Only called at the start
static guint
ufo_med_mad_reject_task_get_num_inputs (UfoTask *task)
{
  return 1;
}

// Only called at the start (returns the dimension of data returned when processing each frame)
static guint
ufo_med_mad_reject_task_get_num_dimensions (UfoTask *task,
                                            guint input)
{
  return 2;
}

// Only called at the start
static UfoTaskMode
ufo_med_mad_reject_task_get_mode (UfoTask *task)
{
  //  return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU; // Requesting also the GPU for computation
  return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_GPU; // Requesting also the GPU for computation
}

// Actual process function :
static gboolean
ufo_med_mad_reject_task_process (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoBuffer *output,
                                 UfoRequisition *requisition)
{
  UfoMedMadRejectTaskPrivate *priv;
  UfoBuffer *swapper;

  priv = UFO_MED_MAD_REJECT_TASK_GET_PRIVATE (task);
  g_debug("%s (l%d) : entering process with index %d, generate index %d, proc_was_called %s", __func__, __LINE__, priv->proc_call_number, priv->gene_call_number, (priv->proc_was_called)? "TRUE" : "FALSE");

  switch ( priv->proc_call_number ) {
  case 0:
    priv->in0 = ufo_buffer_dup(inputs[0]);
    priv->in1 = ufo_buffer_dup(inputs[0]);
    priv->in2 = ufo_buffer_dup(inputs[0]);

    ufo_buffer_copy(inputs[0], priv->in0);
    ufo_buffer_copy(inputs[0], priv->in1);
    priv->proc_call_number += 1;
    priv->proc_was_called = TRUE;
    g_debug ("%s (l%d) : returning TRUE from process with process index %d, generate index %d, proc_was_called %s", __func__, __LINE__, priv->proc_call_number, priv->gene_call_number, (priv->proc_was_called)? "TRUE" : "FALSE");
    return TRUE;

  case 1:
    ufo_buffer_copy(inputs[0], priv->in2);
    break;

  default :
    swapper = priv->in0;
    priv->in0 = priv->in1;
    priv->in1 = priv->in2;
    priv->in2 = swapper;

    ufo_buffer_copy(inputs[0], priv->in2);
    break;
  }

  priv->proc_call_number += 1;
  priv->proc_was_called = TRUE;

  g_debug ("%s (l%d) : returning FALSE from process with process index %d, generate index %d, proc_was_called %s", __func__, __LINE__, priv->proc_call_number, priv->gene_call_number, (priv->proc_was_called)? "TRUE" : "FALSE");
  return FALSE; // returning TRUE means ready to process another frame (kind of)
  // On REDUCTOR FALSE means scheduler has to call generate
}

static gboolean
ufo_med_mad_reject_task_generate (UfoTask *task,
                                  UfoBuffer *output,
                                  UfoRequisition *requisition)
{
  UfoMedMadRejectTaskPrivate *priv;
  UfoGpuNode *node;
  UfoProfiler *profiler;
  cl_command_queue cmd_queue;
  UfoBuffer *swapper=NULL;

  cl_mem in0_mem, in1_mem, in2_mem;
  cl_mem out_mem;

  priv = UFO_MED_MAD_REJECT_TASK_GET_PRIVATE (task);

  if ( priv->gene_shortcut ) {
    g_debug ("%s (l%d) : shortcutting generate with process index %d, generate index %d, proc_was_called %s", __func__, __LINE__, priv->proc_call_number, priv->gene_call_number, (priv->proc_was_called)? "TRUE" : "FALSE");
    priv->gene_shortcut = FALSE;
    return FALSE;
  }

  node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
  cmd_queue = ufo_gpu_node_get_cmd_queue (node);
  out_mem = ufo_buffer_get_device_array (output, cmd_queue);

  g_debug ("%s (l%d) : entering generate with process index %d, generate index %d, proc_was_called %s", __func__, __LINE__, priv->proc_call_number, priv->gene_call_number, (priv->proc_was_called)? "TRUE" : "FALSE");

  if ( !priv->proc_was_called ) { // Special case, we are handling the last view
    swapper = priv->in0;
    priv->in0 = priv->in1;
    priv->in1 = priv->in2;
  }

  in0_mem = ufo_buffer_get_device_array (priv->in0, cmd_queue);
  in1_mem = ufo_buffer_get_device_array (priv->in1, cmd_queue);
  in2_mem = ufo_buffer_get_device_array (priv->in2, cmd_queue);

  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &in0_mem));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &in1_mem));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_mem), &in2_mem));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (cl_mem), &out_mem));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 4, sizeof (cl_float), &priv->threshold));

  profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
  ufo_profiler_call (profiler, cmd_queue, priv->kernel, 2, requisition->dims, NULL);

  if ( !priv->proc_was_called ) { // Special case, we are handling the last view
    priv->in2 = swapper;
    // so that all buffers are released at object deallocation.
  }

  priv->proc_was_called = FALSE;
  priv->gene_shortcut = ( 0 != priv->gene_call_number );;

  priv->gene_call_number += 1;

  g_debug ("%s (l%d) : shortcut %s from generate with process index %d, generate index %d, proc_was_called %s", __func__, __LINE__, (priv->gene_shortcut)? "TRUE":"FALSE", priv->proc_call_number, priv->gene_call_number, (priv->proc_was_called)? "TRUE" : "FALSE");
  return TRUE;
}

static void
ufo_med_mad_reject_task_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
  UfoMedMadRejectTaskPrivate *priv = UFO_MED_MAD_REJECT_TASK_GET_PRIVATE (object);

  switch (property_id) {
  case PROP_THRESHOLD:
    priv->threshold = g_value_get_float (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
ufo_med_mad_reject_task_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  UfoMedMadRejectTaskPrivate *priv = UFO_MED_MAD_REJECT_TASK_GET_PRIVATE (object);

  switch (property_id) {
  case PROP_THRESHOLD:
    g_value_set_float (value, priv->threshold);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
ufo_med_mad_reject_task_finalize (GObject *object)
{
  // Releasing the resources held in the private structure...
  UfoMedMadRejectTaskPrivate *priv = UFO_MED_MAD_REJECT_TASK_GET_PRIVATE (object);

  if ( priv->in0 )
    g_object_unref(priv->in0);
  if ( priv->in1 )
    g_object_unref(priv->in1);
  if ( priv->in2 )
    g_object_unref(priv->in2);

  if ( priv->kernel )
    UFO_RESOURCES_CHECK_CLERR(clReleaseKernel(priv->kernel));

  G_OBJECT_CLASS (ufo_med_mad_reject_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
  iface->setup = ufo_med_mad_reject_task_setup;
  iface->get_num_inputs = ufo_med_mad_reject_task_get_num_inputs;
  iface->get_num_dimensions = ufo_med_mad_reject_task_get_num_dimensions;
  iface->get_mode = ufo_med_mad_reject_task_get_mode;
  iface->get_requisition = ufo_med_mad_reject_task_get_requisition;
  iface->process = ufo_med_mad_reject_task_process;
  iface->generate = ufo_med_mad_reject_task_generate; // Added to provide gobject with that additional method (since REDUCTOR).
}

static void
ufo_med_mad_reject_task_class_init (UfoMedMadRejectTaskClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->set_property = ufo_med_mad_reject_task_set_property;
  oclass->get_property = ufo_med_mad_reject_task_get_property;
  oclass->finalize = ufo_med_mad_reject_task_finalize;

  properties[PROP_THRESHOLD] =
    g_param_spec_float ("threshold",
                        "Rejection threshold",
                        "Pixel value replaced by median when more than threshold away from it (in term of MAD).",
                        0.0f, G_MAXFLOAT, 3.0f,
                        G_PARAM_READWRITE);

  for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
    g_object_class_install_property (oclass, i, properties[i]);

  g_type_class_add_private (oclass, sizeof(UfoMedMadRejectTaskPrivate));
}

static void
ufo_med_mad_reject_task_init(UfoMedMadRejectTask *self)
{
  self->priv = UFO_MED_MAD_REJECT_TASK_GET_PRIVATE(self);
  self->priv->threshold = 3.0f;

  self->priv->in0 = NULL;
  self->priv->in1 = NULL;
  self->priv->in2 = NULL;

  self->priv->proc_was_called = FALSE;
  self->priv->gene_shortcut = FALSE;
  self->priv->proc_call_number = 0;
  self->priv->gene_call_number = 0;
}
