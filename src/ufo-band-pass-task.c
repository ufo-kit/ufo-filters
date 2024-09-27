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

#include "ufo-band-pass-task.h"


struct _UfoBandPassTaskPrivate {
  gboolean zero_frequency;
  gfloat f_0;
  gfloat f_1;
  gfloat s_0;
  gfloat s_1;
  cl_kernel kernel;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoBandPassTask, ufo_band_pass_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_BAND_PASS_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_BAND_PASS_TASK, UfoBandPassTaskPrivate))

enum {
    PROP_0,
    PROP_FREQ_0,
    PROP_FREQ_1,
    PROP_SIGMA_0,
    PROP_SIGMA_1,
    PROP_ZERO_FREQUENCY,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_band_pass_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_BAND_PASS_TASK, NULL));
}

static void
ufo_band_pass_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
  UfoBandPassTaskPrivate *priv;

  priv = UFO_BAND_PASS_TASK_GET_PRIVATE (task);

  priv->kernel = ufo_resources_get_kernel (resources, "bandpass.cl", "bandpass", NULL, error);
  UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->kernel), error);
  

}

static void
ufo_band_pass_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition,
                                 GError **error)
{
  UfoBandPassTaskPrivate *priv;
  priv = UFO_BAND_PASS_TASK_GET_PRIVATE (task);
  ufo_buffer_get_requisition (inputs[0], requisition);
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof(float), &priv->f_0));
  

  int zero_frequency = priv->zero_frequency;
  
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof(float), &priv->f_1));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 4, sizeof(float), &priv->s_0));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 5, sizeof(float), &priv->s_1));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 6, sizeof(int), &zero_frequency));

  
    
}

static guint
ufo_band_pass_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_band_pass_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_band_pass_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_band_pass_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
  UfoBandPassTaskPrivate *priv;
  UfoGpuNode *node;
  cl_command_queue cmd_queue;
  cl_mem in_mem;
  cl_mem out_mem;

  priv = UFO_BAND_PASS_TASK_GET_PRIVATE (task);
  node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
  cmd_queue = ufo_gpu_node_get_cmd_queue (node);

  in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
  out_mem = ufo_buffer_get_device_array (output, cmd_queue);

  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof(cl_mem), &in_mem));
  UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof(cl_mem), &out_mem));
  UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue,
						     priv->kernel,
						     2, NULL, requisition->dims, NULL,
						     0, NULL, NULL));

  
  return TRUE;
}


static void
ufo_band_pass_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  UfoBandPassTaskPrivate *priv = UFO_BAND_PASS_TASK_GET_PRIVATE (object);

  switch (property_id) {
  case PROP_FREQ_0:
    priv->f_0 = g_value_get_float(value);
    break;
  case PROP_FREQ_1:
    priv->f_1 = g_value_get_float(value);
    break;
  case PROP_SIGMA_0:
    priv->s_0 = g_value_get_float(value);
    break;
  case PROP_SIGMA_1:
    priv->s_1 = g_value_get_float(value);
    break;
  case PROP_ZERO_FREQUENCY:
    priv->zero_frequency = g_value_get_boolean(value);
    break;
    
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
ufo_band_pass_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
  UfoBandPassTaskPrivate *priv = UFO_BAND_PASS_TASK_GET_PRIVATE (object);
  
  switch (property_id) {
  case PROP_FREQ_0:
    g_value_set_float(value, priv->f_0);
    break;
  case PROP_FREQ_1:
    g_value_set_float(value, priv->f_1);
    break;
  case PROP_SIGMA_0:
    g_value_set_float(value, priv->s_0);
    break;
  case PROP_SIGMA_1:
    g_value_set_float(value, priv->s_1);
    break;
  case PROP_ZERO_FREQUENCY:
    g_value_set_boolean(value, priv->zero_frequency);
    break;


    
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
ufo_band_pass_task_finalize (GObject *object)
{

    UfoBandPassTaskPrivate *priv = UFO_BAND_PASS_TASK_GET_PRIVATE(object);
    if (priv->kernel){
      UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
      priv->kernel = NULL;
    }
    G_OBJECT_CLASS (ufo_band_pass_task_parent_class)->finalize (object);

}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_band_pass_task_setup;
    iface->get_num_inputs = ufo_band_pass_task_get_num_inputs;
    iface->get_num_dimensions = ufo_band_pass_task_get_num_dimensions;
    iface->get_mode = ufo_band_pass_task_get_mode;
    iface->get_requisition = ufo_band_pass_task_get_requisition;
    iface->process = ufo_band_pass_task_process;
}

static void
ufo_band_pass_task_class_init (UfoBandPassTaskClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->set_property = ufo_band_pass_task_set_property;
  oclass->get_property = ufo_band_pass_task_get_property;
  oclass->finalize = ufo_band_pass_task_finalize;
  
  properties[PROP_FREQ_0] =
    g_param_spec_float ("freq_0",
			"Frequency_0",
			"Test property description blurb",
			0.0,0.5,0.1,
			G_PARAM_READWRITE);
  properties[PROP_FREQ_1] =
    g_param_spec_float ("freq_1",
                        "Frequency_1",
                        "Test property description blurb",
                        0.0,0.5,0.1,
                        G_PARAM_READWRITE);

  properties[PROP_SIGMA_0] =
    g_param_spec_float ("sigma_0",
                        "sigma_0",
                        "Test property description blurb",
                        0.0,10,0.01,
                        G_PARAM_READWRITE);
  properties[PROP_SIGMA_1] =
    g_param_spec_float ("sigma_1",
                        "sigma_1",
                        "Test property description blurb",
                        0.0,10,0.01,
                        G_PARAM_READWRITE);

  properties[PROP_ZERO_FREQUENCY] =
    g_param_spec_boolean ("zero_frequency",
			  "zero_frequency",
			  "Test property description blurb",
			  TRUE,
			  G_PARAM_READWRITE);

  
  
    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoBandPassTaskPrivate));
}

static void
ufo_band_pass_task_init(UfoBandPassTask *self)
{
    self->priv = UFO_BAND_PASS_TASK_GET_PRIVATE(self);
    self->priv->f_0 = 0.1;
    self->priv->f_1 = 0.5;
    self->priv->s_0 = 0.01;
    self->priv->s_1 = 0.01;
    self->priv->zero_frequency = TRUE;
    self->priv->kernel = NULL;
}
