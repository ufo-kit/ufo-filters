/*
 * Instant compiling a one liner OpenCL filter
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
// The following define seems necessary on Linux to get stdio.h declare asprintf family of functions.
// Should be done early enough to be sure that stdio.h is not already included withOUT _GNU_SOURCE on.
#define _GNU_SOURCE
#include <stdio.h> // for asprintf at least.

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif


#include "ufo-ocl-1liner-task.h"

/*
#define IN_LOG printf("=> %s : %s.%d\n", __func__, __FILE__, __LINE__);
#define OUT_LOG printf("OUT %s : %s.%d\n", __func__, __FILE__, __LINE__);
*/

#define IN_LOG
#define OUT_LOG

struct _UfoOCL1LinerTaskPrivate {
  gchar * one_line;
  cl_kernel kernel;
  guint num_inputs;
  gboolean be_quiet;
};


static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoOCL1LinerTask, ufo_ocl_1liner_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_OCL_1LINER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_OCL_1LINER_TASK, UfoOCL1LinerTaskPrivate))

enum {
  PROP_0,
  PROP_ONE_LINE,
  PROP_NUM_INPUTS,
  PROP_QUIET,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_ocl_1liner_task_new (void)
{
  return UFO_NODE (g_object_new (UFO_TYPE_OCL_1LINER_TASK, NULL));
}

static void
ufo_ocl_1liner_task_setup (UfoTask *task,
                           UfoResources *resources,
                           GError **error)
{
  IN_LOG
  UfoOCL1LinerTaskPrivate *priv;
  priv = UFO_OCL_1LINER_TASK_GET_PRIVATE (task);

  /* Preparing the kernel source to be compiled */
  gchar * kernel_skel = NULL;
  char * kernel_src = NULL;

  const gchar * skel_filename = "ocl-1liner-skel.cl";

  kernel_skel = ufo_resources_get_kernel_source (resources, skel_filename, error);

  char skel_in_macro[1024];
  char skel_in[1024];
  int one_in_macro_size;
  int one_in_size;
  char * skel_in_macro_next = skel_in_macro;
  char * skel_in_next = skel_in;

  for ( guint i=0; i != priv->num_inputs; ++i ) {
    one_in_macro_size = snprintf(skel_in_macro_next, 1024 - (skel_in_macro_next - skel_in_macro),
				 "#define in_%d_px (in_%d[px_index])\n", i, i);
    one_in_size = snprintf(skel_in_next, 1024 - (skel_in_next - skel_in),
			   "__global float *in_%d,\n", i);

    if ( (0 >= one_in_macro_size) || (0 >= one_in_size) )
      goto exit;

    skel_in_macro_next += one_in_macro_size;
    skel_in_next += one_in_size;
  }

  asprintf(&kernel_src, kernel_skel, skel_in_macro, skel_in, priv->one_line);
  if ( ! priv->be_quiet ) {
    /* Done with the preparation of the OpenCL sources. */
    fprintf(stdout, "Current version of the one-liner OpenCL source code :\n%s\n", kernel_src);
  }

  /* Copiling the kernel now : */
  priv->kernel = ufo_resources_get_kernel_from_source (resources, kernel_src, "ocl_1liner", NULL, error);
  /* Done compiling sources into a kernel, if existing retain it */
  if (priv->kernel != NULL)
    UFO_RESOURCES_CHECK_AND_SET (clRetainKernel (priv->kernel), error);

 exit:
  /* Releasing resources no longer used */
  g_free (kernel_skel);
  free (kernel_src);
  OUT_LOG
}

static void
ufo_ocl_1liner_task_get_requisition (UfoTask *task,
                                     UfoBuffer **inputs,
                                     UfoRequisition *requisition,
                                     GError **error)
{
  // In the current version of the kernel all the inputs are supposed to have the same dimensions
  // Or more precisely the output has the same dimension as first input (indexed 0) and one work-item
  // is run for each pixel of input 0 (hence for each pixel of the output).
  ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_ocl_1liner_task_get_num_inputs (UfoTask *task)
{
  IN_LOG
  UfoOCL1LinerTaskPrivate *priv;
  priv = UFO_OCL_1LINER_TASK_GET_PRIVATE (task);

  OUT_LOG
  return priv->num_inputs;
}

static guint
ufo_ocl_1liner_task_get_num_dimensions (UfoTask *task,
                                        guint input)
{
  return 2;
}

static UfoTaskMode
ufo_ocl_1liner_task_get_mode (UfoTask *task)
{
  // Running as a single image stream processing and using OpenCL for GPU computation
  return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_ocl_1liner_task_process (UfoTask *task,
                             UfoBuffer **inputs,
                             UfoBuffer *output,
                             UfoRequisition *requisition)
{
  IN_LOG
  UfoOCL1LinerTaskPrivate *priv;
  priv = UFO_OCL_1LINER_TASK_GET_PRIVATE (task);

  UfoGpuNode *node;
  UfoProfiler *profiler;
  cl_command_queue cmd_queue;
  cl_mem in_mem;
  cl_mem out_mem;

  node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
  cmd_queue = ufo_gpu_node_get_cmd_queue (node);
  profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

  // Taking care of setting the inputs :
  for ( guint i=0; i != priv->num_inputs; ++i ) {
    in_mem = ufo_buffer_get_device_array (inputs[i], cmd_queue);
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, i, sizeof (cl_mem), &in_mem));
  }

  // Taking care of setting the output :
  out_mem = ufo_buffer_get_device_array (output, cmd_queue);
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, priv->num_inputs, sizeof (cl_mem), &out_mem));

  // Finally launching the kernel :
  ufo_profiler_call (profiler, cmd_queue, priv->kernel, 2, requisition->dims, NULL);

  // Done, returning (TRUE):
  OUT_LOG
  return TRUE;
}


static void
ufo_ocl_1liner_task_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  IN_LOG
  UfoOCL1LinerTaskPrivate *priv = UFO_OCL_1LINER_TASK_GET_PRIVATE (object);

  switch (property_id) {
  case PROP_ONE_LINE:
    g_free (priv->one_line);
    priv->one_line = g_value_dup_string (value);
    break;
  case PROP_NUM_INPUTS:
    priv->num_inputs = g_value_get_uint (value);
    break;
  case PROP_QUIET:
    priv->be_quiet = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
  OUT_LOG
}

static void
ufo_ocl_1liner_task_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  IN_LOG
  UfoOCL1LinerTaskPrivate *priv = UFO_OCL_1LINER_TASK_GET_PRIVATE (object);

  switch (property_id) {
  case PROP_ONE_LINE:
    g_value_set_string (value, priv->one_line);
    break;
  case PROP_NUM_INPUTS:
    g_value_set_uint (value, priv->num_inputs);
    break;
  case PROP_QUIET:
    g_value_set_boolean (value, priv->be_quiet);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
  OUT_LOG
}

static void
ufo_ocl_1liner_task_finalize (GObject *object)
{
  IN_LOG
  UfoOCL1LinerTaskPrivate *priv = UFO_OCL_1LINER_TASK_GET_PRIVATE (object);
  g_free (priv->one_line);

  if ( priv->kernel )
    UFO_RESOURCES_CHECK_CLERR(clReleaseKernel(priv->kernel));

  G_OBJECT_CLASS (ufo_ocl_1liner_task_parent_class)->finalize (object);
  OUT_LOG
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
  IN_LOG
  iface->setup = ufo_ocl_1liner_task_setup;
  iface->get_num_inputs = ufo_ocl_1liner_task_get_num_inputs;
  iface->get_num_dimensions = ufo_ocl_1liner_task_get_num_dimensions;
  iface->get_mode = ufo_ocl_1liner_task_get_mode;
  iface->get_requisition = ufo_ocl_1liner_task_get_requisition;
  iface->process = ufo_ocl_1liner_task_process;
  OUT_LOG
}

static void
ufo_ocl_1liner_task_class_init (UfoOCL1LinerTaskClass *klass)
{
  IN_LOG
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->set_property = ufo_ocl_1liner_task_set_property;
  oclass->get_property = ufo_ocl_1liner_task_get_property;
  oclass->finalize = ufo_ocl_1liner_task_finalize;

  properties[PROP_ONE_LINE] =
    g_param_spec_string ("one-line",
                         "The one line C/OpenCL computation to perform",
                         "* in0 .. inN are input array(s).\n"
                         "* out is 1D output array.\n"
                         "\n"
                         "Those can be addressed using px_index which is the /current/ pixel for the workitem.\n"
                         "\n"
                         "One can also access a /random/ pixel value using the macro : IMG_VAL which takes three\n"
                         "arguments : the (x, y) pixel coordinate, and the array pointer for the image.\n"
                         "NB : this macro acn be used also for lvalue (but it is risky that a work-item\n"
                         "modifies a pixel value in =out= that is NOT the one indexed by px_index.\n"
                         "\n"
                         "Examples :\n"
                         "* using the OpenCL sqrt function :\n"
                         "out[px_index] = sqrt(in0[px_index])"
                         "* Using the other adressing methods and the image size to shift one pixel to the left\n",
                         "out[px_index] = IMG_VAL((x<(sizeX-1))?x+1:(sizeX-1),y,in0)",
                         G_PARAM_READWRITE);

  properties[PROP_NUM_INPUTS] =
    g_param_spec_uint ("num-inputs",
                       "Number of input streams.",
                       "Number of input streams, which will be labelled in0, in1 ... in(n-1).",
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE);


  properties[PROP_QUIET] =
    g_param_spec_boolean("quiet",
                         "When turned to false, will display the generated kernel source to stdout",
                         "Defaulting to 'true', minimising output",
                         TRUE,
                         G_PARAM_READWRITE);

  for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
    g_object_class_install_property (oclass, i, properties[i]);

  g_type_class_add_private (oclass, sizeof(UfoOCL1LinerTaskPrivate));
  OUT_LOG
}

static void
ufo_ocl_1liner_task_init(UfoOCL1LinerTask *self)
{
  IN_LOG
  self->priv = UFO_OCL_1LINER_TASK_GET_PRIVATE(self);
  self->priv->one_line = NULL;
  self->priv->kernel = NULL;
  self->priv->num_inputs=1;
  self->priv->be_quiet=TRUE;
  OUT_LOG
}
