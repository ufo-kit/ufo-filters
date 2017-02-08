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

#include <string.h>
#include <math.h>
#include "ufo-rofex-mask-sino-task.h"

const gfloat OFFSET_DENOMINATOR = 360.0;

struct _UfoRofexMaskSinoTaskPrivate {
    cl_context context;
    cl_kernel  mask_kernel;
    UfoBuffer  *mask_buf;

    gfloat source_offset;
    gfloat lower_limit_offset;
    gfloat upper_limit_offset;
    guint xa;
    guint xb;
    guint xc;
    guint xd;
    guint xe;
    guint xf;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexMaskSinoTask, ufo_rofex_mask_sino_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_MASK_SINO_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_MASK_SINO_TASK, UfoRofexMaskSinoTaskPrivate))

enum {
    PROP_0,
    PROP_SOURCE_OFFSET,
    PROP_LOWER_LIMIT_OFFSET,
    PROP_UPPER_LIMIT_OFFSET,
    PROP_XA,
    PROP_XB,
    PROP_XC,
    PROP_XD,
    PROP_XE,
    PROP_XF,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_mask_sino_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_MASK_SINO_TASK, NULL));
}

static void
make_mask(gfloat *mask,
          gsize mask_size,
          guint n_proj,
          guint n_dets,
          gfloat source_offset,
          gfloat lower_limit_offset,
          gfloat upper_limit_offset,
          guint xa, guint xb, guint xc, guint xd, guint xe, guint xf)
{
  guint ya, yb, yc, yd, ye;
  guint y_max, y_min;

  gfloat lower_limit = (lower_limit_offset + source_offset)/OFFSET_DENOMINATOR;
  gfloat upper_limit = (upper_limit_offset + source_offset)/OFFSET_DENOMINATOR;

  memset(mask, 1.0, mask_size);

  ya = round(lower_limit * n_proj);
  yb = ya;
  yc = round(upper_limit * n_proj);
  yd = yc;

  // slope of the straight
  gfloat m = ((gfloat)ya - (gfloat)yd) / ((gfloat)xa - (gfloat)xd);

  ye = round( (gfloat)yc + ((gfloat)xe - (gfloat)xc)*m );

  for (guint x = 0; x <= xa; x++) {
    y_min = ya;
    y_max = round(ye + m * x);
    for (guint y = y_min; y < y_max; y++)
        mask[x + y * n_dets] = 0.0;
  }

  for (guint x = xa; x <= xc; x++) {
    y_min = round(ya + m * (x - xa));
    y_max = round(ye + m * x);
    for (guint y = y_min; y < y_max; y++)
        mask[x + y * n_dets] = 0.0;
  }

  for (guint x = xc; x <= xd; x++) {
    y_min = round(ya + m * (x - xa));
    y_max = yd;
    for (guint y = y_min; y < y_max; y++)
        mask[x + y * n_dets] = 0.0;
  }

  for (guint x = xb; x <= xf; x++) {
    y_min = yb;
    y_max = round(yb + m * (x - xb));
    for (guint y = y_min; y < y_max; y++)
        mask[x + y * n_dets] = 0.0;
  }

  guint nvals = lower_limit * n_dets * n_proj;
  memset(mask, 0.0, nvals * sizeof(gfloat));

  nvals = upper_limit * n_dets * n_proj;
  memset(mask + nvals, 0.0, mask_size - nvals * sizeof(gfloat));
}

static void
ufo_rofex_mask_sino_task_setup (UfoTask *task,
                                UfoResources *resources,
                                GError **error)
{
    UfoRofexMaskSinoTaskPrivate *priv;
    priv = UFO_ROFEX_MASK_SINO_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    priv->mask_kernel = ufo_resources_get_kernel (resources, "rofex.cl", "mask_sino", error);
    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->mask_kernel));
}

static void
ufo_rofex_mask_sino_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    ufo_buffer_get_requisition(inputs[0], requisition);
}

static guint
ufo_rofex_mask_sino_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_mask_sino_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_mask_sino_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_rofex_mask_sino_task_process (UfoTask *task,
                                  UfoBuffer **inputs,
                                  UfoBuffer *output,
                                  UfoRequisition *requisition)
{
    UfoRofexMaskSinoTaskPrivate *priv;
    priv = UFO_ROFEX_MASK_SINO_TASK_GET_PRIVATE (task);
    guint n_dets = requisition->dims[0];
    guint n_proj = requisition->dims[1];

    //
    // Validate that mask exists and matches to the input data
    UfoRequisition mask_req;
    mask_req.n_dims = 2;
    mask_req.dims[0] = n_dets;
    mask_req.dims[1] = n_proj;

    gboolean recalculate_mask = FALSE;

    if (priv->mask_buf == NULL) {
        priv->mask_buf = ufo_buffer_new(&mask_req, priv->context);
        recalculate_mask = TRUE;
    }

    UfoRequisition old_mask_req;
    ufo_buffer_get_requisition(priv->mask_buf, &old_mask_req);

    if (old_mask_req.dims[0] != mask_req.dims[0] ||
        old_mask_req.dims[1] != mask_req.dims[1])
    {
        ufo_buffer_resize(priv->mask_buf, &mask_req);
        recalculate_mask = TRUE;
    }

    if (recalculate_mask) {
        guint mask_size = ufo_buffer_get_size(priv->mask_buf);
        gfloat *h_mask  = ufo_buffer_get_host_array(priv->mask_buf, NULL);

        make_mask(h_mask,
                  mask_size,
                  n_proj,
                  n_dets,
                  priv->source_offset,
                  priv->lower_limit_offset,
                  priv->upper_limit_offset,
                  priv->xa, priv->xb, priv->xc, priv->xd, priv->xe, priv->xf);
    }

    // Get plane ID for the sinogram
    GValue *gv_plane_index;
    gv_plane_index = ufo_buffer_get_metadata (inputs[0], "plane-index");
    guint plane_index = g_value_get_uint (gv_plane_index);

    // Apply mask
    UfoGpuNode *node;
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));

    cl_command_queue cmd_queue;
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    gpointer d_sino = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    gpointer d_out = ufo_buffer_get_device_array (output, cmd_queue);
    gpointer d_mask = ufo_buffer_get_device_array (priv->mask_buf, cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mask_kernel, 0, sizeof (cl_mem), &d_sino));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mask_kernel, 1, sizeof (cl_mem), &d_mask));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mask_kernel, 2, sizeof (cl_mem), &d_out));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mask_kernel, 3, sizeof (guint), &n_dets));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->mask_kernel, 4, sizeof (guint), &n_proj));

    UfoProfiler *profiler;
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->mask_kernel,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);

    return TRUE;
}


static void
ufo_rofex_mask_sino_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexMaskSinoTaskPrivate *priv = UFO_ROFEX_MASK_SINO_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SOURCE_OFFSET:
            priv->source_offset = g_value_get_float(value);
            break;
        case PROP_LOWER_LIMIT_OFFSET:
            priv->lower_limit_offset = g_value_get_float(value);
            break;
        case PROP_UPPER_LIMIT_OFFSET:
            priv->upper_limit_offset = g_value_get_float(value);
            break;
        case PROP_XA:
            priv->xa = g_value_get_uint(value);
            break;
        case PROP_XB:
            priv->xb = g_value_get_uint(value);
            break;
        case PROP_XC:
            priv->xc = g_value_get_uint(value);
            break;
        case PROP_XD:
            priv->xd = g_value_get_uint(value);
            break;
        case PROP_XE:
            priv->xe = g_value_get_uint(value);
            break;
        case PROP_XF:
            priv->xf = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_mask_sino_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexMaskSinoTaskPrivate *priv = UFO_ROFEX_MASK_SINO_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_SOURCE_OFFSET:
            g_value_set_float(value, priv->source_offset);
            break;
        case PROP_LOWER_LIMIT_OFFSET:
            g_value_set_float(value, priv->lower_limit_offset);
            break;
        case PROP_UPPER_LIMIT_OFFSET:
            g_value_set_float(value, priv->upper_limit_offset);
            break;
        case PROP_XA:
            g_value_set_uint(value, priv->xa);
            break;
        case PROP_XB:
            g_value_set_uint(value, priv->xb);
            break;
        case PROP_XC:
            g_value_set_uint(value, priv->xc);
            break;
        case PROP_XD:
            g_value_set_uint(value, priv->xd);
            break;
        case PROP_XE:
            g_value_set_uint(value, priv->xe);
            break;
        case PROP_XF:
            g_value_set_uint(value, priv->xf);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_mask_sino_task_finalize (GObject *object)
{
    UfoRofexMaskSinoTaskPrivate *priv;
    priv = UFO_ROFEX_MASK_SINO_TASK_GET_PRIVATE (object);

    if (priv->mask_kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->mask_kernel));
        priv->mask_kernel = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    G_OBJECT_CLASS (ufo_rofex_mask_sino_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_mask_sino_task_setup;
    iface->get_num_inputs = ufo_rofex_mask_sino_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_mask_sino_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_mask_sino_task_get_mode;
    iface->get_requisition = ufo_rofex_mask_sino_task_get_requisition;
    iface->process = ufo_rofex_mask_sino_task_process;
}

static void
ufo_rofex_mask_sino_task_class_init (UfoRofexMaskSinoTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_mask_sino_task_set_property;
    oclass->get_property = ufo_rofex_mask_sino_task_get_property;
    oclass->finalize = ufo_rofex_mask_sino_task_finalize;

    properties[PROP_SOURCE_OFFSET] =
              g_param_spec_float ("source-offset",
                                  "Source offset.",
                                  "Source offset.",
                                  G_MINFLOAT, G_MAXFLOAT, 23.2,
                                  G_PARAM_READWRITE);

    properties[PROP_LOWER_LIMIT_OFFSET] =
              g_param_spec_float ("lower-limit-offset",
                                  "The lower limit for offset.",
                                  "The lower limit for offset.",
                                  G_MINFLOAT, G_MAXFLOAT, 47.0,
                                  G_PARAM_READWRITE);

    properties[PROP_UPPER_LIMIT_OFFSET] =
              g_param_spec_float ("upper-limit-offset",
                                  "The upper limit for offset.",
                                  "The upper limit for offset.",
                                  G_MINFLOAT, G_MAXFLOAT, 313.0,
                                  G_PARAM_READWRITE);

    properties[PROP_XA] =
              g_param_spec_uint ("xa",
                                 "X-offset to point A",
                                 "X-offset to point A",
                                 0, G_MAXUINT, 43,
                                 G_PARAM_READWRITE);

    properties[PROP_XB] =
              g_param_spec_uint ("xb",
                                 "X-offset to point B",
                                 "X-offset to point B",
                                 0, G_MAXUINT, 285,
                                 G_PARAM_READWRITE);

    properties[PROP_XC] =
              g_param_spec_uint ("xc",
                                 "X-offset to point C",
                                 "X-offset to point C",
                                 0, G_MAXUINT, 175,
                                 G_PARAM_READWRITE);

    properties[PROP_XD] =
              g_param_spec_uint ("xd",
                                 "X-offset to point D",
                                 "X-offset to point D",
                                 0, G_MAXUINT, 362,
                                 G_PARAM_READWRITE);

    properties[PROP_XE] =
              g_param_spec_uint ("xe",
                                 "X-offset to point E",
                                 "X-offset to point E",
                                 0, G_MAXUINT, 0,
                                 G_PARAM_READWRITE);

    properties[PROP_XF] =
              g_param_spec_uint ("xf",
                                 "X-offset to point F",
                                 "X-offset to point F",
                                 0, G_MAXUINT, 431,
                                 G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexMaskSinoTaskPrivate));
}

static void
ufo_rofex_mask_sino_task_init(UfoRofexMaskSinoTask *self)
{
    self->priv = UFO_ROFEX_MASK_SINO_TASK_GET_PRIVATE(self);
    self->priv->source_offset = 23.2;
    self->priv->lower_limit_offset = 47.0;
    self->priv->upper_limit_offset = 313.0;
    self->priv->xa = 43;
    self->priv->xb = 285;
    self->priv->xc = 175;
    self->priv->xd = 362;
    self->priv->xe = 0;
    self->priv->xf = 431;
}
