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
#include "ufo-rofex-reorder-task.h"


/*
  Description:
  This filter accepts a stack of 2D images, each image of which consists of
  measurements made by detector modules for a particular (plane, frame).
  Each row represents a series of projections made by a single detector module.
  The filter reorders the data in such way that each row of the image will
  consist of data related to a specific projection, and the data in a row will
  be ordered according to the order respective detector modules.

  This filter needs a reordering schema, which has the following format:
    Type: guint
    InputIndex for OutputIndex=0, InputIndex for OutputIndex=1, ...
    InputIndex for OutputIndex=N

  Input:
  A stack of 2D images, where each image represents data collected by all
  detector modules for a particular (plane, frame):
    0: nDetsPerModule * nProjections
    1: nDetModules
    2: portionSize

  Output:
  A stack of 2D images, i.e. the stack of fan-beam sinograms:
    0: nDetsPerModule * nDetModules
    1: nProjections
    2: portionSize
*/

/*
  Algorithm for building the schema:
    guint i = 0;
    for (guint proj = 0; proj < n_fan_proj; proj++) {
      for (guint module = 0; module < n_modules; module++) {
        for (guint det = 0; det < n_det_per_module; det++) {
          schema[i++] = det + proj * n_det_per_module
                      + module * (n_det_per_module * n_fan_proj);
        }
      }
    }
*/


struct _UfoRofexReorderTaskPrivate {
  gchar *reorder_schema_path;
  guint n_det_per_module;
  guint n_planes;

  cl_mem d_reorder_schema;
  cl_kernel reorder_kernel;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexReorderTask, ufo_rofex_reorder_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_REORDER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_REORDER_TASK, UfoRofexReorderTaskPrivate))

enum {
    PROP_0,
    // Precomputed properties
    PROP_REORDER_SCHEMA_PATH,
    // ROFEX properties
    PROP_N_DET_PER_MODULE,
    PROP_N_PLANES,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_reorder_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_REORDER_TASK, NULL));
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

    // ---------- Kernel
    priv->reorder_kernel = ufo_resources_get_kernel (resources,
                                                     "rofex.cl",
                                                     "reorder",
                                                     error);

    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->reorder_kernel));

    // ---------- Read reordering schema.
    priv->d_reorder_schema = load_data_gpu (priv->reorder_schema_path,
                                            context,
                                            cmd_queue,
                                            error);
}

static void
ufo_rofex_reorder_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexReorderTaskPrivate *priv;
    UfoRequisition req;
    guint n_fan_proj, n_modules, n_images;

    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    n_fan_proj = req.dims[0] / priv->n_det_per_module;
    n_modules = req.dims[1];
    n_images = req.dims[2];

    requisition->n_dims = 3;
    requisition->dims[0] = priv->n_det_per_module * n_modules;
    requisition->dims[1] = n_fan_proj;
    requisition->dims[2] = n_images;
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
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_kernel kernel;

    gpointer d_input, d_output;
    guint n_fan_dets, n_fan_proj, n_images;

    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    kernel = priv->reorder_kernel;

    d_input = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    d_output = ufo_buffer_get_device_array(output, cmd_queue);
    n_fan_dets = requisition->dims[0];
    n_fan_proj = requisition->dims[1];
    n_images = requisition->dims[2];

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (kernel, 0, sizeof (cl_mem), &d_input));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (kernel, 1, sizeof (cl_mem), &d_output));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (kernel, 2, sizeof (cl_mem), &priv->d_reorder_schema));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (kernel, 3, sizeof (guint), &n_fan_dets));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (kernel, 4, sizeof (guint), &n_fan_proj));

    UFO_RESOURCES_CHECK_CLERR (\
      clSetKernelArg (kernel, 5, sizeof (guint), &n_images));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       kernel,
                       requisition->n_dims,
                       requisition->dims,
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
    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_REORDER_SCHEMA_PATH:
            priv->reorder_schema_path = g_value_dup_string(value);
            break;
        case PROP_N_DET_PER_MODULE:
            priv->n_det_per_module = g_value_get_uint(value);
            break;
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
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
        case PROP_REORDER_SCHEMA_PATH:
            g_value_set_string(value, priv->reorder_schema_path);
            break;
        case PROP_N_DET_PER_MODULE:
            g_value_set_uint (value, priv->n_det_per_module);
            break;
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
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

    if (priv->reorder_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->reorder_kernel));
        priv->reorder_kernel = NULL;
    }

    if (priv->d_reorder_schema != NULL) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->d_reorder_schema));
        priv->d_reorder_schema = NULL;
    }

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

    properties[PROP_REORDER_SCHEMA_PATH] =
                g_param_spec_string ("path-to-reordering-schema",
                                     "Path to the file that stores reordering schema.",
                                     "Path to the file that stores reordering schema.",
                                     "",
                                     G_PARAM_READWRITE);

    properties[PROP_N_DET_PER_MODULE] =
                g_param_spec_uint ("number-of-detectors-per-module",
                                   "The number of pixels per detector module",
                                   "The number of pixels per detector module",
                                   1, G_MAXUINT, 1,
                                   G_PARAM_READWRITE);

    properties[PROP_N_PLANES] =
                g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexReorderTaskPrivate));
}

static void
ufo_rofex_reorder_task_init(UfoRofexReorderTask *self)
{
    self->priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE(self);
    self->priv->reorder_schema_path = g_strdup("");
    self->priv->n_det_per_module = 1;
    self->priv->n_planes = 1;
}
