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

#include "ufo-rofex-convert-task.h"


/*
  DESCRIPTION:
  This filter extracts measured values and amplifies them according to
  the set bits. The filter accepts a stack of 2D images. Each image is
  composed of data received from the related module for a number of the beam
  transitions. The number of beam transitions defines a portion size.

  This filter has to be applied before reordering.

  INPUT:
  A stack of 2D images:
    0: nDetsPerModule * nFanProjections
    1: nTransPerPortion
    2: nModulePairs

  OUTPUT:
  A stack of 2D images:
    0: nDetsPerModule * nFanProjections
    1: nTransPerPortion
    2: nModulePairs
*/


struct _UfoRofexConvertTaskPrivate {
    guint16 amp_bit15;
    guint16 amp_bit16;

    cl_kernel kernel;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexConvertTask, ufo_rofex_convert_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_CONVERT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_CONVERT_TASK, UfoRofexConvertTaskPrivate))

enum {
    PROP_0,
    PROP_AMP_BIT15,
    PROP_AMP_BIT16,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_convert_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_CONVERT_TASK, NULL));
}

static void
ufo_rofex_convert_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexConvertTaskPrivate *priv;
    priv = UFO_ROFEX_CONVERT_TASK_GET_PRIVATE (task);

    //
    // Load kernel
    priv->kernel =
        ufo_resources_get_kernel (resources, "rofex.cl", "amplif", error);

    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
}

static void
ufo_rofex_convert_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    ufo_buffer_get_requisition(inputs[0], requisition);
}

static guint
ufo_rofex_convert_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_convert_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_convert_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_rofex_convert_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexConvertTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;

    gpointer d_input, d_output;
    guint n_vals, n_trans_per_portion, n_modpairs;

    priv = UFO_ROFEX_CONVERT_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    // Move data buffers to GPU if required.
    d_input = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    d_output = ufo_buffer_get_device_array (output, cmd_queue);

    // Move data buffers to GPU if required.
    n_vals = requisition->dims[0];
    n_modpairs = requisition->dims[2];
    n_trans_per_portion = requisition->dims[1];

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &d_input));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &d_output));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 2, sizeof (guint), &n_vals));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 3, sizeof (guint), &n_trans_per_portion));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 4, sizeof (guint), &n_modpairs));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 5, sizeof (guint16), &priv->amp_bit15));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 6, sizeof (guint16), &priv->amp_bit16));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->kernel,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);

    return TRUE;
}


static void
ufo_rofex_convert_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexConvertTaskPrivate *priv;
    priv = UFO_ROFEX_CONVERT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_AMP_BIT15:
            priv->amp_bit15 = g_value_get_uint(value);
            break;
        case PROP_AMP_BIT16:
            priv->amp_bit16 = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_convert_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexConvertTaskPrivate *priv;
    priv = UFO_ROFEX_CONVERT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_AMP_BIT15:
            g_value_set_uint (value, priv->amp_bit15);
            break;
        case PROP_AMP_BIT16:
            g_value_set_uint (value, priv->amp_bit16);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_convert_task_finalize (GObject *object)
{
    UfoRofexConvertTaskPrivate *priv;
    priv = UFO_ROFEX_CONVERT_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    G_OBJECT_CLASS (ufo_rofex_convert_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_convert_task_setup;
    iface->get_num_inputs = ufo_rofex_convert_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_convert_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_convert_task_get_mode;
    iface->get_requisition = ufo_rofex_convert_task_get_requisition;
    iface->process = ufo_rofex_convert_task_process;
}

static void
ufo_rofex_convert_task_class_init (UfoRofexConvertTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_convert_task_set_property;
    oclass->get_property = ufo_rofex_convert_task_get_property;
    oclass->finalize = ufo_rofex_convert_task_finalize;

    properties[PROP_AMP_BIT15] =
        g_param_spec_uint ("amplifier-15bit",
                           "Amplification from the 15th bit.",
                           "Amplification from the 15th bit.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE);

    properties[PROP_AMP_BIT16] =
        g_param_spec_uint ("amplifier-16bit",
                           "Amplification from the 16th bit.",
                           "Amplification from the 16th bit.",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexConvertTaskPrivate));
}

static void
ufo_rofex_convert_task_init(UfoRofexConvertTask *self)
{
    self->priv = UFO_ROFEX_CONVERT_TASK_GET_PRIVATE(self);
    self->priv->amp_bit15 = 0;
    self->priv->amp_bit16 = 0;
}
