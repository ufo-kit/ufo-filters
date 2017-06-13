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
#include "ufo-rofex-fan2par-task.h"


/*
  DESCRIPTION:
  The filter converts the sinogram from fan to parallel beam geometry.

  REQUIREMENTS:
    - Precomputed parameters

  INPUT:
  A stack of 2D images, i.e. the stack of fan-beam sinograms:
    0: nModsPerRing * nDetsPerModule
    1: nFanProjections
    2: nTransPerPortion * ringsSelectionMaskSize

  OUTPUT:
  A stack of 2D images, i.e. the stack of parallel-beam sinograms:
    0: nParDetectors
    1: nParProjections
    2: nTransPerPortion * ringsSelectionMaskSize
*/


struct _UfoRofexFan2parTaskPrivate {
    guint n_rings;
    guint n_par_dets;
    guint n_par_proj;
    guint detector_diameter;
    GValueArray *gv_beam_positions;
    GValueArray *gv_rings_selection_mask;
    gchar *params_path;

    cl_mem d_params;
    cl_mem d_beam_positions;
    cl_mem d_rings_selection_mask;
    cl_kernel kernel;
    cl_kernel kernel_set_zero;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexFan2parTask, ufo_rofex_fan2par_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_FAN2PAR_TASK, UfoRofexFan2parTaskPrivate))

enum {
    PROP_0,
    PROP_N_RINGS,
    PROP_N_PAR_DETECTORS,
    PROP_N_PAR_PROJECTIONS,
    PROP_DETECTOR_DIAMETER,
    PROP_BEAM_POSITIONS,
    PROP_RINGS_SELECTION_MASK,
    PROP_PATH_TO_PARAMS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_fan2par_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_FAN2PAR_TASK, NULL));
}

static void
ufo_rofex_fan2par_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexFan2parTaskPrivate *priv;
    UfoGpuNode *node;
    gpointer cmd_queue, context;

    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    context = ufo_resources_get_context (resources);

    //
    // Load kernel
    priv->kernel =
        ufo_resources_get_kernel (resources, "rofex.cl", "fan2par_interp", error);

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
    // Load parameters to GPU
    priv->d_params = read_file_to_gpu (priv->params_path,
                                       context,
                                       cmd_queue,
                                       error);
}

static void
ufo_rofex_fan2par_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);

    ufo_buffer_get_requisition(inputs[0], requisition);
    requisition->dims[0] = priv->n_par_dets;
    requisition->dims[1] = priv->n_par_proj;
}

static guint
ufo_rofex_fan2par_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_fan2par_task_get_num_dimensions (UfoTask *task,
                                           guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_fan2par_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_rofex_fan2par_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexFan2parTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;

    UfoRequisition req;
    gpointer d_input, d_output;
    guint n_fan_dets, n_fan_proj, n_par_dets, n_par_proj, n_sinos;
    guint n_trans_per_portion, n_beam_positions, rings_selection_mask_size;
    guint param_offset;
    gfloat detector_r;

    GValue *gv_portion;
    guint portion;

    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    ufo_buffer_get_requisition(inputs[0], &req);

    // Move data buffers to GPU if required.
    d_input = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    d_output = ufo_buffer_get_device_array(output, cmd_queue);

    // Compute and extract required parameters.
    n_fan_dets = req.dims[0];
    n_fan_proj = req.dims[1];
    n_par_dets = requisition->dims[0];
    n_par_proj = requisition->dims[1];
    n_sinos = requisition->dims[2];

    n_beam_positions = priv->gv_beam_positions->n_values;
    rings_selection_mask_size = priv->gv_rings_selection_mask->n_values;
    n_trans_per_portion = n_sinos / rings_selection_mask_size;

    detector_r = priv->detector_diameter / 2.0;
    param_offset = (n_par_dets * n_par_proj * 2) * priv->n_rings;

    // Fill with zeros
    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 0, sizeof (cl_mem), &d_output));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 1, sizeof (guint), &n_par_dets));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 2, sizeof (guint), &n_par_proj));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel_set_zero, 3, sizeof (guint), &n_sinos));

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
    grid.dims[0] = n_par_dets;
    grid.dims[1] = n_par_proj;
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

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 6, sizeof (guint), &n_par_dets));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 7, sizeof (guint), &n_par_proj));

    // ROFEX parameters
    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 8, sizeof (gfloat), &detector_r));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 9, sizeof (guint), &priv->n_rings));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 10, sizeof (cl_mem), &priv->d_beam_positions));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 11, sizeof (guint), &n_beam_positions));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 12, sizeof (cl_mem), &priv->d_rings_selection_mask));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 13, sizeof (guint), &rings_selection_mask_size));

    // Precomputed
    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 14, sizeof (cl_mem), &priv->d_params));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (priv->kernel, 15, sizeof (guint), &param_offset));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->kernel,
                       grid.n_dims,
                       grid.dims,
                       NULL);

    return TRUE;
}


static void
ufo_rofex_fan2par_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFan2parTaskPrivate *priv;
    GValueArray *array;

    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
            priv->n_rings = g_value_get_uint(value);
            break;
        case PROP_N_PAR_DETECTORS:
            priv->n_par_dets = g_value_get_uint(value);
            break;
        case PROP_N_PAR_PROJECTIONS:
            priv->n_par_proj = g_value_get_uint(value);
            break;
        case PROP_DETECTOR_DIAMETER:
            priv->detector_diameter = g_value_get_float(value);
            break;
        case PROP_PATH_TO_PARAMS:
            priv->params_path = g_value_dup_string(value);
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
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_fan2par_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_RINGS:
            g_value_set_uint (value, priv->n_rings);
            break;
        case PROP_N_PAR_DETECTORS:
            g_value_set_uint(value, priv->n_par_dets);
            break;
        case PROP_N_PAR_PROJECTIONS:
            g_value_set_uint(value, priv->n_par_proj);
            break;
        case PROP_DETECTOR_DIAMETER:
            g_value_set_float(value, priv->detector_diameter);
            break;
        case PROP_BEAM_POSITIONS:
            g_value_set_boxed (value, priv->gv_beam_positions);
            break;
        case PROP_RINGS_SELECTION_MASK:
            g_value_set_boxed (value, priv->gv_rings_selection_mask);
            break;
        case PROP_PATH_TO_PARAMS:
            g_value_set_string(value, priv->params_path);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_fan2par_task_finalize (GObject *object)
{
    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (object);

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

    if (priv->d_params != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->d_params));
        priv->d_params = NULL;
    }

    g_value_array_free (priv->gv_beam_positions);
    g_value_array_free (priv->gv_rings_selection_mask);
    g_free(priv->params_path);

    G_OBJECT_CLASS (ufo_rofex_fan2par_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_fan2par_task_setup;
    iface->get_num_inputs = ufo_rofex_fan2par_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_fan2par_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_fan2par_task_get_mode;
    iface->get_requisition = ufo_rofex_fan2par_task_get_requisition;
    iface->process = ufo_rofex_fan2par_task_process;
}

static void
ufo_rofex_fan2par_task_class_init (UfoRofexFan2parTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_fan2par_task_set_property;
    oclass->get_property = ufo_rofex_fan2par_task_get_property;
    oclass->finalize = ufo_rofex_fan2par_task_finalize;

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
                           "The number of rings.",
                           1, G_MAXUINT, 2,
                           G_PARAM_READWRITE);

    properties[PROP_N_PAR_DETECTORS] =
        g_param_spec_uint ("number-of-parallel-detectors",
                           "The number of pixels in a parallel projection",
                           "The number of pixels in a parallel projection",
                           1, G_MAXUINT, 256,
                           G_PARAM_READWRITE);

    properties[PROP_N_PAR_PROJECTIONS] =
        g_param_spec_uint ("number-of-parallel-projections",
                           "The number of parallel projection",
                           "The number of parallel projection",
                           1, G_MAXUINT, 512,
                           G_PARAM_READWRITE);

    properties[PROP_DETECTOR_DIAMETER] =
        g_param_spec_float ("detector-diameter",
                            "Detector diameter.",
                            "Detector diameter.",
                            0, G_MAXFLOAT, 216.0,
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

    properties[PROP_PATH_TO_PARAMS] =
        g_param_spec_string ("path-to-params",
                             "Path to the raw file that stores parameters.",
                             "Path to the raw file that stores parameters.",
                             "",
                             G_PARAM_READWRITE);


    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexFan2parTaskPrivate));
}

static void
ufo_rofex_fan2par_task_init(UfoRofexFan2parTask *self)
{
    self->priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE(self);
    self->priv->n_rings = 2;
    self->priv->n_par_dets = 256;
    self->priv->n_par_proj = 512;
    self->priv->detector_diameter = 216.0;
    self->priv->params_path = g_strdup ("");

    set_default_rings_selection_mask (&self->priv->gv_rings_selection_mask);
    set_default_beam_positions (&self->priv->gv_beam_positions);
}
