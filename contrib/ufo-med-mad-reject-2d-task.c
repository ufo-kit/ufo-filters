/*
 * Replacing pixels by local median value when detected as outliers based on MAD (2D box, variable size)
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

#include <stdio.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-med-mad-reject-2d-task.h"


struct _UfoMedMadReject2DTaskPrivate {
  gfloat threshold;
  guint32 box_size;
  cl_kernel kernel;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoMedMadReject2DTask, ufo_med_mad_reject_2d_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_MED_MAD_REJECT_2D_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_MED_MAD_REJECT_2D_TASK, UfoMedMadReject2DTaskPrivate))

enum {
  PROP_0,
  PROP_THRESHOLD,
  PROP_BOX_SIZE,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_med_mad_reject_2d_task_new (void)
{
  return UFO_NODE (g_object_new (UFO_TYPE_MED_MAD_REJECT_2D_TASK, NULL));
}

static void
ufo_med_mad_reject_2d_task_setup (UfoTask *task,
                                  UfoResources *resources,
                                  GError **error)
{
  UfoMedMadReject2DTaskPrivate *priv;

  priv = UFO_MED_MAD_REJECT_2D_TASK_GET_PRIVATE (task);

  // Checking that the threshold is positive (being 0, makes it a median filter)
  if ( priv->threshold <= 0.0f ) {
    g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                 "Threshold value %f is not positive",
                 priv->threshold);
    return;
  }

  // Checking that the box size is odd :
  if ( ! (0x1 & priv->box_size) ) {
    g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                 "Boxsize value %u is not odd",
                 priv->box_size);
    return;
  }

  char kernel_opts[1024];
  snprintf(kernel_opts, 1023, "-DBOXSIZE=%u", priv->box_size);
  priv->kernel = ufo_resources_get_kernel (resources, "med-mad-reject-2d.cl", "med_mad_rej_2D", kernel_opts, error);

  if (priv->kernel != NULL)
    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
}

static void
ufo_med_mad_reject_2d_task_get_requisition (UfoTask *task,
                                            UfoBuffer **inputs,
                                            UfoRequisition *requisition,
                                            GError **error)
{
  // Transferring the buffer «size» (size, data type and dimension) to the output
  ufo_buffer_get_requisition(inputs[0], requisition);
}

static guint
ufo_med_mad_reject_2d_task_get_num_inputs (UfoTask *task)
{
  return 1;
}

static guint
ufo_med_mad_reject_2d_task_get_num_dimensions (UfoTask *task,
                                               guint input)
{
  return 2;
}

static UfoTaskMode
ufo_med_mad_reject_2d_task_get_mode (UfoTask *task)
{
  return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU; // This is a processor type task, using GPU.
}

static gboolean
ufo_med_mad_reject_2d_task_process (UfoTask *task,
                                    UfoBuffer **inputs,
                                    UfoBuffer *output,
                                    UfoRequisition *requisition)
{
  UfoMedMadReject2DTaskPrivate *priv;
  priv = UFO_MED_MAD_REJECT_2D_TASK_GET_PRIVATE (task);

  UfoGpuNode *node;
  UfoProfiler *profiler;
  cl_command_queue cmd_queue;
  cl_mem in_mem;
  cl_mem out_mem;

  node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
  cmd_queue = ufo_gpu_node_get_cmd_queue (node);

  // making sure the input image is in GPU memory :
  in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
  out_mem = ufo_buffer_get_device_array (output, cmd_queue);

  // Setting up the kernel to run :
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &in_mem));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &out_mem));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_float), &priv->threshold));

  profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
  ufo_profiler_call (profiler, cmd_queue, priv->kernel, 2, requisition->dims, NULL);

  return TRUE;
}


static void
ufo_med_mad_reject_2d_task_set_property (GObject *object,
                                         guint property_id,
                                         const GValue *value,
                                         GParamSpec *pspec)
{
  UfoMedMadReject2DTaskPrivate *priv = UFO_MED_MAD_REJECT_2D_TASK_GET_PRIVATE (object);

  switch (property_id) {
  case PROP_THRESHOLD:
    priv->threshold = g_value_get_float (value);
    break;
  case PROP_BOX_SIZE:
    priv->box_size = g_value_get_uint (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
ufo_med_mad_reject_2d_task_get_property (GObject *object,
                                         guint property_id,
                                         GValue *value,
                                         GParamSpec *pspec)
{
  UfoMedMadReject2DTaskPrivate *priv = UFO_MED_MAD_REJECT_2D_TASK_GET_PRIVATE (object);

  switch (property_id) {
  case PROP_THRESHOLD:
    g_value_set_float (value, priv->threshold);
    break;
  case PROP_BOX_SIZE:
    g_value_set_uint (value, priv->box_size);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
ufo_med_mad_reject_2d_task_finalize (GObject *object)
{
  UfoMedMadReject2DTaskPrivate *priv = UFO_MED_MAD_REJECT_2D_TASK_GET_PRIVATE (object);

  if ( priv->kernel )
    UFO_RESOURCES_CHECK_CLERR(clReleaseKernel(priv->kernel));

  G_OBJECT_CLASS (ufo_med_mad_reject_2d_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
  iface->setup = ufo_med_mad_reject_2d_task_setup;
  iface->get_num_inputs = ufo_med_mad_reject_2d_task_get_num_inputs;
  iface->get_num_dimensions = ufo_med_mad_reject_2d_task_get_num_dimensions;
  iface->get_mode = ufo_med_mad_reject_2d_task_get_mode;
  iface->get_requisition = ufo_med_mad_reject_2d_task_get_requisition;
  iface->process = ufo_med_mad_reject_2d_task_process;
}

static void
ufo_med_mad_reject_2d_task_class_init (UfoMedMadReject2DTaskClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->set_property = ufo_med_mad_reject_2d_task_set_property;
  oclass->get_property = ufo_med_mad_reject_2d_task_get_property;
  oclass->finalize = ufo_med_mad_reject_2d_task_finalize;

  properties[PROP_THRESHOLD] =
    g_param_spec_float ("threshold",
                        "Rejection threshold",
                        "Pixel value replaced by median when more than threshold away from it (in term of MAD).",
                        0.0f, G_MAXFLOAT, 3.0f,
                        G_PARAM_READWRITE);

  properties[PROP_BOX_SIZE] =
    g_param_spec_uint ("box-size",
                        "Size of the box in which the median and mad are computed",
                        "Should be an odd number so that current pixel is the exact center of the box.",
                        1, 1023, 3,
                        G_PARAM_READWRITE);

  for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
    g_object_class_install_property (oclass, i, properties[i]);
  g_type_class_add_private (oclass, sizeof(UfoMedMadReject2DTaskPrivate));
}

static void
ufo_med_mad_reject_2d_task_init(UfoMedMadReject2DTask *self)
{
  self->priv = UFO_MED_MAD_REJECT_2D_TASK_GET_PRIVATE(self);
}
