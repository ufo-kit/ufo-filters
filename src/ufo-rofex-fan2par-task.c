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
#include "ufo-rofex-fan2par-task.h"


/*
  Description:
  This filter accepts a stack of 2D images. Each image of the stack
  represents a fan-beam sinogram that was build using data of all detector
  modules for the related (plane, frame).
  The filter converts the sinogram from fan to parallel beam geometry.

  This filter require a set of precomputed parameters of the transformation.

  Input:
  A stack of 2D images, i.e. the stack of fan-beam sinograms:
    0: nDetsPerModule * nDetModules | nFanDetectors
    1: nProjections                 | nFanProjections
    2: portionSize

  Output:
  A stack of 2D images, i.e. the stack of parallel-beam sinograms:
    0: nParDetectors
    1: nParProjections
    2: portionSize
*/

struct _UfoRofexFan2parTaskPrivate {
    guint n_planes;
    guint n_par_dets;
    guint n_par_proj;
    guint detector_diameter;
    gchar *params_path;

    cl_kernel interp_kernel;
    cl_kernel set_kernel;
    cl_mem    d_params;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexFan2parTask, ufo_rofex_fan2par_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_FAN2PAR_TASK, UfoRofexFan2parTaskPrivate))

enum {
    PROP_0,
    PROP_N_PLANES,
    PROP_N_PAR_DETECTORS,
    PROP_N_PAR_PROJECTIONS,
    PROP_DETECTOR_DIAMETER,
    PROP_PATH_TO_PARAMS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_fan2par_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_FAN2PAR_TASK, NULL));
}

cl_mem
load_data_gpu (const gchar *filepath,
               gpointer  context,
               gpointer  cmd_queue,
               GError **error)
{
    FILE  * pFile;
    gpointer buffer;
    cl_mem d_buffer;
    gsize n_bytes, bytes_read;
    cl_int err;

    pFile = fopen (filepath , "rb");
    if (pFile == NULL) {
        g_error("File %s cannot be read.", filepath);
    }

    fseek (pFile , 0 , SEEK_END);
    n_bytes = ftell (pFile);

    buffer = g_malloc (n_bytes);
    if (buffer == NULL) {
        fclose (pFile);
        g_error ("Memory cannot be allocated (%ld bytes).", n_bytes);
    }

    rewind (pFile);
    bytes_read = fread (buffer, 1, n_bytes, pFile);
    if (bytes_read != n_bytes) {
        fclose (pFile);
        g_free(buffer);
        g_error ("The wrong number of bytes has been read.");
    }

    // Copy on GPU
    d_buffer = clCreateBuffer (context,
                               CL_MEM_READ_WRITE,
                               n_bytes,
                               NULL, &err);

    UFO_RESOURCES_CHECK_CLERR (err);

    err = clEnqueueWriteBuffer (cmd_queue,
                                d_buffer,
                                CL_TRUE,
                                0, n_bytes,
                                buffer,
                                0,
                                NULL, NULL);

    UFO_RESOURCES_CHECK_CLERR (err);
    clFinish (cmd_queue);

    // Free host memory
    g_free (buffer);
    return d_buffer;
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

    // ---------- Kernels
    priv->set_kernel = ufo_resources_get_kernel (resources,
                                                 "rofex.cl",
                                                 "fan2par_set",
                                                  error);
    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->set_kernel));

    priv->interp_kernel = ufo_resources_get_kernel (resources,
                                                    "rofex.cl",
                                                    "fan2par_interp",
                                                    error);
    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->interp_kernel));

    // ---------- Read parameters to gpu.
    priv->d_params = load_data_gpu (priv->params_path,
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
    UfoProfiler *profiler;
    UfoGpuNode *node;
    UfoRequisition req;
    cl_command_queue cmd_queue;
    cl_kernel kernel;

    gpointer d_input, d_output;
    guint n_fan_dets, n_fan_proj;
    guint n_par_dets, n_par_proj;
    guint param_offset;
    gfloat detector_r;
    guint plane_index;
    gboolean single_image;

    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    ufo_buffer_get_requisition(inputs[0], &req);
    n_fan_dets = req.dims[0];
    n_fan_proj = req.dims[1];

    n_par_dets = priv->n_par_dets;
    n_par_proj = priv->n_par_proj;

    param_offset = n_par_dets * (n_par_proj * 2) * priv->n_planes;
    detector_r = priv->detector_diameter / 2.0;

    // --- Plane index
    single_image = req.n_dims == 2 || req.n_dims == 3 && req.dims[2] == 1;
    if (single_image) {
        GValue *gv_plane_index = NULL;
        gv_plane_index = ufo_buffer_get_metadata(inputs[0], "plane-index");
        plane_index = gv_plane_index ? g_value_get_uint (gv_plane_index) : 0;
    } else {
        plane_index = req.dims[2] % priv->n_planes;
    }

    // --- Data
    d_input = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    d_output = ufo_buffer_get_device_array(output, cmd_queue);

    // --- Set output zero
    kernel = priv->set_kernel;

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 0, sizeof (cl_mem), &d_output));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 1, sizeof (guint), &n_par_dets));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 2, sizeof (guint), &n_par_proj));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       kernel,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);

    // --- Convert
    kernel = priv->interp_kernel;

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 0, sizeof (cl_mem), &d_input));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 1, sizeof (cl_mem), &d_output));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 2, sizeof (cl_mem), &priv->d_params));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 3, sizeof (guint), &param_offset));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 4, sizeof (guint), &n_fan_dets));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 5, sizeof (guint), &n_fan_proj));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 6, sizeof (guint), &n_par_dets));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 7, sizeof (guint), &n_par_proj));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 8, sizeof (guint), &priv->n_planes));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 9, sizeof (gfloat), &detector_r));

    UFO_RESOURCES_CHECK_CLERR (\
        clSetKernelArg (kernel, 10, sizeof (gint), &plane_index));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       kernel,
                       requisition->n_dims,
                       requisition->dims,
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
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
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
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
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

    if (priv->set_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->set_kernel));
        priv->set_kernel = NULL;
    }

    if (priv->interp_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->interp_kernel));
        priv->interp_kernel = NULL;
    }

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

    properties[PROP_N_PLANES] =
                g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
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
    self->priv->n_planes = 1;
    self->priv->n_par_dets = 256;
    self->priv->n_par_proj = 512;
    self->priv->detector_diameter = 216.0;
    self->priv->params_path = g_strdup ("");
}
