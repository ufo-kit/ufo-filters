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

#include <stdio.h>
#include "rofex.h"
#include "ufo-rofex-attenuation-task.h"


/*
  DESCRIPTION:
  The filter computes attenuation for the measured values.

  REQUIREMENTS:
  - Flat-fields (result of process-flats-task)
  - Dark-fields (result of process-darks-task)

  INPUT:
  A stack of 2D images, i.e. the stack of fan-beam sinograms:
    0: nModsPerRing * nDetsPerModule
    1: nFanProjections
    2: nTransPerPortion * ringsSelectionMaskSize

  OUTPUT:
  A stack of 2D images, i.e. the stack of fan-beam sinograms:
    0: nModsPerRing * nDetsPerModule
    1: nFanProjections
    2: nTransPerPortion * ringsSelectionMaskSize
*/


struct _UfoRofexAttenuationTaskPrivate {
    guint n_rings;
    GValueArray *gv_beam_positions;
    GValueArray *gv_rings_selection_mask;
    gchar *avg_darks_path;
    gchar *avg_flats_path;

    cl_mem d_beam_positions;
    cl_mem d_rings_selection_mask;
    cl_mem d_avg_flats;
    cl_mem d_avg_darks;
    cl_kernel kernel;
    cl_kernel kernel_set_zero;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexAttenuationTask, ufo_rofex_attenuation_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_ATTENUATION_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_ATTENUATION_TASK, UfoRofexAttenuationTaskPrivate))

enum {
    PROP_0,
    PROP_N_RINGS,
    PROP_BEAM_POSITIONS,
    PROP_RINGS_SELECTION_MASK,
    PROP_AVERAGED_FLATS_PATH,
    PROP_AVERAGED_DARKS_PATH,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_attenuation_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_ATTENUATION_TASK, NULL));
}

static void
ufo_rofex_attenuation_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexAttenuationTaskPrivate *priv;
    UfoGpuNode *node;
    gpointer cmd_queue, context;

    priv = UFO_ROFEX_ATTENUATION_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    context = ufo_resources_get_context (resources);

    //
    // Load kernel
    priv->kernel =
        ufo_resources_get_kernel (resources, "rofex.cl", "attenuation", error);

    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));

    priv->kernel_set_zero =
        ufo_resources_get_kernel (resources, "rofex.cl", "fill_zeros", error);

    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel_set_zero));

    //
    // Copy the beam positions to GPU
    priv->d_beam_positions =
            copy_gvarray_guint_to_gpu(priv->gv_beam_positions,
                                      context,
                                      cmd_queue,
                                      error);

    if (error && *error)
         return;

    //
    // Copy the rings selection mask to GPU
    priv->d_rings_selection_mask =
            copy_gvarray_gint_to_gpu(priv->gv_rings_selection_mask,
                                     context,
                                     cmd_queue,
                                     error);
    if (error && *error)
         return;

    //
    // Load the averaged dark- and flat-fields to GPU
    priv->d_avg_flats = read_file_to_gpu (priv->avg_flats_path,
                                          context,
                                          cmd_queue,
                                          error);
    if (error && *error)
        return;

    priv->d_avg_darks = read_file_to_gpu (priv->avg_darks_path,
                                          context,
                                          cmd_queue,
                                          error);
    if (error && *error)
        return;
}

static void
ufo_rofex_attenuation_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    ufo_buffer_get_requisition(inputs[0], requisition);
}

static guint
ufo_rofex_attenuation_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_attenuation_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_attenuation_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_rofex_attenuation_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexAttenuationTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;

    gpointer d_input, d_output;
    guint n_fan_dets, n_fan_proj, n_fan_sinos;
    guint n_trans_per_portion, n_beam_positions, rings_selection_mask_size;

    GValue *gv_portion;
    guint portion;

    priv = UFO_ROFEX_ATTENUATION_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    // Move data buffers to GPU if required.
    d_input = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    d_output = ufo_buffer_get_device_array(output, cmd_queue);

    // Compute and extract required parameters.
    n_fan_dets = requisition->dims[0];
    n_fan_proj = requisition->dims[1];
    n_fan_sinos = requisition->dims[2];

    n_beam_positions = priv->gv_beam_positions->n_values;
    rings_selection_mask_size = priv->gv_rings_selection_mask->n_values;
    n_trans_per_portion = n_fan_sinos / rings_selection_mask_size;

    // Fill with zeros
    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 0, sizeof (cl_mem), &d_output));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 1, sizeof (guint), &n_fan_dets));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 2, sizeof (guint), &n_fan_proj));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 3, sizeof (guint), &n_fan_sinos));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->kernel_set_zero,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);

    // Get portion ID.
    gv_portion = ufo_buffer_get_metadata(inputs[0], "portion");
    portion = gv_portion ? g_value_get_uint (gv_portion) : 0;

    // Run processing.
    UfoRequisition grid;
    grid.n_dims = 3;
    grid.dims[0] = n_fan_dets;
    grid.dims[1] = n_fan_proj;
    grid.dims[2] = n_trans_per_portion;

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &d_input));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &d_output));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 2, sizeof (guint), &portion));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 3, sizeof (guint), &n_trans_per_portion));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 4, sizeof (guint), &n_fan_dets));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 5, sizeof (guint), &n_fan_proj));

    // ROFEX parameters
    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 6, sizeof (guint), &priv->n_rings));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 7, sizeof (cl_mem), &priv->d_beam_positions));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 8, sizeof (guint), &n_beam_positions));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 9, sizeof (cl_mem), &priv->d_rings_selection_mask));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 10, sizeof (guint), &rings_selection_mask_size));

    // Precomputed
    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 11, sizeof (cl_mem), &priv->d_avg_flats));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 12, sizeof (cl_mem), &priv->d_avg_darks));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->kernel,
                       grid.n_dims,
                       grid.dims,
                       NULL);

    return TRUE;
}


static void
ufo_rofex_attenuation_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexAttenuationTaskPrivate *priv;
    GValueArray *array;

    priv = UFO_ROFEX_ATTENUATION_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
          priv->n_rings = g_value_get_uint(value);
          break;
        case PROP_BEAM_POSITIONS:
            array = (GValueArray *) g_value_get_boxed (value);
            g_value_array_free (priv->gv_beam_positions);
            priv->gv_beam_positions = g_value_array_copy (array);
            break;
        case PROP_RINGS_SELECTION_MASK:
            array = (GValueArray *) g_value_get_boxed (value);
            g_value_array_free (priv->gv_rings_selection_mask);
            priv->gv_rings_selection_mask = g_value_array_copy (array);
            break;
        case PROP_AVERAGED_FLATS_PATH:
            priv->avg_flats_path = g_value_dup_string(value);
            break;
        case PROP_AVERAGED_DARKS_PATH:
            priv->avg_darks_path = g_value_dup_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_attenuation_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexAttenuationTaskPrivate *priv;
    priv = UFO_ROFEX_ATTENUATION_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
          g_value_set_uint (value, priv->n_rings);
          break;
        case PROP_BEAM_POSITIONS:
            g_value_set_boxed (value, priv->gv_beam_positions);
            break;
        case PROP_RINGS_SELECTION_MASK:
            g_value_set_boxed (value, priv->gv_rings_selection_mask);
            break;
        case PROP_AVERAGED_FLATS_PATH:
            g_value_set_string(value, priv->avg_flats_path);
            break;
        case PROP_AVERAGED_DARKS_PATH:
            g_value_set_string(value, priv->avg_darks_path);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_attenuation_task_finalize (GObject *object)
{
    UfoRofexAttenuationTaskPrivate *priv;
    priv = UFO_ROFEX_ATTENUATION_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->d_avg_flats != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->d_avg_flats));
        priv->d_avg_flats = NULL;
    }

    if (priv->d_avg_darks != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->d_avg_darks));
        priv->d_avg_darks = NULL;
    }

    g_value_array_free (priv->gv_beam_positions);
    g_value_array_free (priv->gv_rings_selection_mask);
    g_free (priv->avg_flats_path);
    g_free (priv->avg_darks_path);

    G_OBJECT_CLASS (ufo_rofex_attenuation_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_attenuation_task_setup;
    iface->get_num_inputs = ufo_rofex_attenuation_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_attenuation_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_attenuation_task_get_mode;
    iface->get_requisition = ufo_rofex_attenuation_task_get_requisition;
    iface->process = ufo_rofex_attenuation_task_process;
}

static void
ufo_rofex_attenuation_task_class_init (UfoRofexAttenuationTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_attenuation_task_set_property;
    oclass->get_property = ufo_rofex_attenuation_task_get_property;
    oclass->finalize = ufo_rofex_attenuation_task_finalize;

    GParamSpec *rings_selection_mask_vals =
        g_param_spec_int ("rings-selection-mask-values",
                          "Values of the rings selection mask.",
                          "Elements in rings selection mask",
                          G_MININT,
                          G_MAXINT,
                          (gint) 0,
                          G_PARAM_READWRITE);

    GParamSpec *beam_positions_vals =
        g_param_spec_uint ("beam-positions-values",
                           "Values of the beam positions.",
                           "Elements in beam positions",
                           0,
                           G_MAXUINT,
                           (guint) 0,
                           G_PARAM_READWRITE);

    properties[PROP_N_RINGS] =
        g_param_spec_uint ("number-of-rings",
                           "The number of rings.",
                           "The number of rings. ",
                           1, G_MAXUINT, 2,
                           G_PARAM_READWRITE);

    properties[PROP_BEAM_POSITIONS] =
        g_param_spec_value_array ("beam-positions",
                                  "Order in which the beam hits the rings.",
                                  "Order in which the beam hits the rings.",
                                  beam_positions_vals,
                                  G_PARAM_READWRITE);

    properties[PROP_RINGS_SELECTION_MASK] =
        g_param_spec_value_array ("rings-selection-mask",
                                  "Offsets to the affected rings around the ring hitted by the beam.",
                                  "Offsets to the affected rings around the ring hitted by the beam.",
                                  rings_selection_mask_vals,
                                  G_PARAM_READWRITE);

    properties[PROP_AVERAGED_FLATS_PATH] =
        g_param_spec_string ("path-to-averaged-flats",
                             "Path to the result of averaging flat fields (raw format).",
                             "Path to the result of averaging flat fields (raw format).",
                             "",
                             G_PARAM_READWRITE);

    properties[PROP_AVERAGED_DARKS_PATH] =
        g_param_spec_string ("path-to-averaged-darks",
                             "Path to the result of averaging dark fields (raw format).",
                             "Path to the result of averaging dark fields (raw format).",
                             "",
                             G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexAttenuationTaskPrivate));
}

static void
ufo_rofex_attenuation_task_init(UfoRofexAttenuationTask *self)
{
    self->priv = UFO_ROFEX_ATTENUATION_TASK_GET_PRIVATE(self);
    self->priv->n_rings = 2;
    self->priv->avg_flats_path = g_strdup("");
    self->priv->avg_darks_path = g_strdup("");

    set_default_rings_selection_mask (&self->priv->gv_rings_selection_mask);
    set_default_beam_positions (&self->priv->gv_beam_positions);
}
