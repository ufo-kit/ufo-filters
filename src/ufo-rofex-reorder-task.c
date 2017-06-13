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
#include "ufo-rofex-reorder-task.h"


/*
  DESCRIPTION:
  The filter reorders data collected by all modules to get a stack of
  fan-beam sinograms. The stack is divided into sectors consisting of the
  sinograms that describe slices through the rings selected according to
  the rings selection mask. Each sector represents a time moment, i.e.
  transition of the beam.

  If selected ring does not exist, then the corresponding sinogram is
  filled with zeros.

  To determine the ring which the sinogram describes, one should use
  the ordered beam positions and the transition index.

  REQUIREMENTS:
  - Detectors activation map.
    Detectors activation map represents has the same dimensions as
    a stack of fan-beam sinograms, which number equals to the number of rings.
    The values stored in the map represent detector pixels activated at
    particular angles. The indices are increased by one to make masking
    possible.

  INPUT:
  A stack of 2D images:
    0: nDetsPerModule * nFanProjections
    1: nTransPerPortion
    2: nModulePairs

  OUTPUT:
  A stack of 2D images, i.e. the stack of fan-beam sinograms:
    0: nModsPerRing * nDetsPerModule
    1: nFanProjections
    2: nTransPerPortion * ringsSelectionMaskSize
*/


struct _UfoRofexReorderTaskPrivate {
  guint n_rings;
  guint n_mods_per_ring;
  guint n_dets_per_module;
  GValueArray *gv_beam_positions;
  GValueArray *gv_rings_selection_mask;
  gchar       *dets_map_path;

  cl_mem d_beam_positions;
  cl_mem d_rings_selection_mask;
  cl_mem d_dets_map;
  cl_kernel kernel;
  cl_kernel kernel_set_zero;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexReorderTask, ufo_rofex_reorder_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_REORDER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_REORDER_TASK, UfoRofexReorderTaskPrivate))

enum {
    PROP_0,
    PROP_N_RINGS,
    PROP_N_MODS_PER_RING,
    PROP_N_DETS_PER_MODULE,
    PROP_BEAM_POSITIONS,
    PROP_RINGS_SELECTION_MASK,
    PROP_DETS_MAP_PATH,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_reorder_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_REORDER_TASK, NULL));
}

static void
ufo_rofex_reorder_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexReorderTaskPrivate *priv;
    UfoGpuNode *node;
    gpointer context;
    cl_command_queue cmd_queue;

    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    context = ufo_resources_get_context (resources);

    //
    // Load kernel
    priv->kernel =
        ufo_resources_get_kernel (resources, "rofex.cl", "reorder", error);

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
    // Load the detector activation map to GPU
    priv->d_dets_map = read_file_to_gpu (priv->dets_map_path,
                                         context,
                                         cmd_queue,
                                         error);

    if (error && *error)
        return;
}

static void
ufo_rofex_reorder_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexReorderTaskPrivate *priv;
    UfoRequisition req;
    guint n_trans_per_portion, rings_selection_mask_size;
    guint n_fan_dets, n_fan_proj, n_fan_sinos;

    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    n_trans_per_portion = req.dims[1];
    rings_selection_mask_size = priv->gv_rings_selection_mask->n_values;

    n_fan_dets = priv->n_mods_per_ring * priv->n_dets_per_module;
    n_fan_proj = req.dims[0] / priv->n_dets_per_module;
    n_fan_sinos = n_trans_per_portion * rings_selection_mask_size;

    requisition->n_dims = 3;
    requisition->dims[0] = n_fan_dets;
    requisition->dims[1] = n_fan_proj;
    requisition->dims[2] = n_fan_sinos;
}

static guint
ufo_rofex_reorder_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_reorder_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_reorder_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_rofex_reorder_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexReorderTaskPrivate *priv;
    UfoGpuNode  *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;

    UfoRequisition req;
    gpointer d_input, d_output;
    guint n_fan_proj, n_fan_dets;
    guint n_trans_per_portion, n_beam_positions, rings_selection_mask_size;
    guint n_given_modpairs, n_modpairs_per_ring, n_expected_modpairs;

    GValue *gv_portion;
    guint portion;

    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    ufo_buffer_get_requisition(inputs[0], &req);

    // Move data buffers to GPU if required.
    d_input = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    d_output = ufo_buffer_get_device_array(output, cmd_queue);

    // Fill with zeros
    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 0, sizeof (cl_mem), &d_output));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 1, sizeof (guint), &requisition->dims[0]));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 2, sizeof (guint), &requisition->dims[1]));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 3, sizeof (guint), &requisition->dims[2]));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->kernel_set_zero,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);


    // Compute and extract required parameters.
    n_fan_proj = req.dims[0] / priv->n_dets_per_module;
    n_fan_dets = priv->n_mods_per_ring * priv->n_dets_per_module;
    n_trans_per_portion = req.dims[1];
    n_beam_positions = priv->gv_beam_positions->n_values;
    rings_selection_mask_size = priv->gv_rings_selection_mask->n_values;

    n_given_modpairs = req.dims[2];
    n_modpairs_per_ring = priv->n_mods_per_ring / 2;
    n_expected_modpairs = n_modpairs_per_ring * priv->n_rings;

    // Validate the given data.
    if (n_given_modpairs < n_expected_modpairs) {
        g_error("Unexpected number of pairs of modules (given=%d, expected=%d).",
                n_given_modpairs, n_expected_modpairs);
        return FALSE;
    }

    // Get portion ID.
    gv_portion = ufo_buffer_get_metadata(inputs[0], "portion");
    portion = gv_portion ? g_value_get_uint (gv_portion) : 0;

    // Compute the grid and run processing.
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
      clSetKernelArg (priv->kernel, 7, sizeof (guint), &priv->n_dets_per_module));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 8, sizeof (guint), &priv->n_mods_per_ring));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 9, sizeof (cl_mem), &priv->d_beam_positions));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 10, sizeof (guint), &n_beam_positions));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 11, sizeof (cl_mem), &priv->d_rings_selection_mask));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 12, sizeof (guint), &rings_selection_mask_size));

    // Precomputed
    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 13, sizeof (cl_mem), &priv->d_dets_map));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->kernel,
                       grid.n_dims,
                       grid.dims,
                       NULL);

    return TRUE;
}

static void
ufo_rofex_reorder_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexReorderTaskPrivate *priv;
    GValueArray *array;

    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
            priv->n_rings = g_value_get_uint (value);
            break;
        case PROP_N_MODS_PER_RING:
            priv->n_mods_per_ring = g_value_get_uint (value);
            break;
        case PROP_N_DETS_PER_MODULE:
            priv->n_dets_per_module = g_value_get_uint (value);
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
        case PROP_DETS_MAP_PATH:
            priv->dets_map_path = g_value_dup_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_reorder_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexReorderTaskPrivate *priv;
    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
            g_value_set_uint (value, priv->n_rings);
            break;
        case PROP_N_MODS_PER_RING:
            g_value_set_uint (value, priv->n_mods_per_ring);
            break;
        case PROP_N_DETS_PER_MODULE:
            g_value_set_uint (value, priv->n_dets_per_module);
            break;
        case PROP_BEAM_POSITIONS:
            g_value_set_boxed (value, priv->gv_beam_positions);
            break;
        case PROP_RINGS_SELECTION_MASK:
            g_value_set_boxed (value, priv->gv_rings_selection_mask);
            break;
        case PROP_DETS_MAP_PATH:
            g_value_set_string(value, priv->dets_map_path);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_reorder_task_finalize (GObject *object)
{
    UfoRofexReorderTaskPrivate *priv;
    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->kernel_set_zero) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel_set_zero));
        priv->kernel_set_zero = NULL;
    }

    if (priv->d_beam_positions != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->d_beam_positions));
        priv->d_beam_positions = NULL;
    }

    if (priv->d_rings_selection_mask != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->d_rings_selection_mask));
        priv->d_rings_selection_mask = NULL;
    }

    if (priv->d_dets_map != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->d_dets_map));
        priv->d_dets_map = NULL;
    }

    g_value_array_free (priv->gv_beam_positions);
    g_value_array_free (priv->gv_rings_selection_mask);
    g_free(priv->dets_map_path);

    G_OBJECT_CLASS (ufo_rofex_reorder_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_reorder_task_setup;
    iface->get_num_inputs = ufo_rofex_reorder_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_reorder_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_reorder_task_get_mode;
    iface->get_requisition = ufo_rofex_reorder_task_get_requisition;
    iface->process = ufo_rofex_reorder_task_process;
}

static void
ufo_rofex_reorder_task_class_init (UfoRofexReorderTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_reorder_task_set_property;
    oclass->get_property = ufo_rofex_reorder_task_get_property;
    oclass->finalize = ufo_rofex_reorder_task_finalize;

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

    properties[PROP_N_MODS_PER_RING] =
        g_param_spec_uint ("number-of-modules-per-ring",
                           "The number of modules per ring.",
                           "The number of modules per ring.",
                           1, G_MAXUINT, 18,
                           G_PARAM_READWRITE);

    properties[PROP_N_DETS_PER_MODULE] =
        g_param_spec_uint ("number-of-detectors-per-module",
                           "The number of detectors per module.",
                           "The number of detectors per module.",
                           1, G_MAXUINT, 16,
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

    properties[PROP_DETS_MAP_PATH] =
        g_param_spec_string ("path-to-detectors-map",
                             "Path to the detectors activation map.",
                             "Path to the detectors activation map.",
                             "",
                             G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexReorderTaskPrivate));
}

static void
ufo_rofex_reorder_task_init(UfoRofexReorderTask *self)
{
    self->priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE(self);
    self->priv->n_rings = 2;
    self->priv->n_mods_per_ring = 18;
    self->priv->n_dets_per_module = 16;
    self->priv->dets_map_path = g_strdup("");

    set_default_rings_selection_mask (&self->priv->gv_rings_selection_mask);
    set_default_beam_positions (&self->priv->gv_beam_positions);
}
