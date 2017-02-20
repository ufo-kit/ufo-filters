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
#include "ufo-rofex-reorder-task.h"

static void
reorder(gfloat *input,
        gfloat *output,
        guint plane_index,
        guint n_dets,
        guint n_modules,
        guint n_proj,
        guint n_planes);

struct _UfoRofexReorderTaskPrivate {
    // Rofex configuration
    guint n_modules;
    guint n_det_per_module;
    guint n_planes;

    //
    guint n_produced;
    gboolean generated;
    UfoBuffer *frame_buf;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexReorderTask, ufo_rofex_reorder_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_REORDER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_REORDER_TASK, UfoRofexReorderTaskPrivate))

enum {
    PROP_0,
    PROP_N_MODULES,
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

static void
ufo_rofex_reorder_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexReorderTaskPrivate *priv;
    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);

    priv->n_produced = 0;
    priv->generated = FALSE;
}

static void
ufo_rofex_reorder_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexReorderTaskPrivate *priv;
    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);

    UfoRequisition fb_req;
    ufo_buffer_get_requisition(inputs[0], &fb_req);

    guint n_modules = priv->n_modules;
    guint n_det_per_module = priv->n_det_per_module;
    guint n_proj = fb_req.dims[0] / n_det_per_module;

    requisition->n_dims = 2;
    requisition->dims[0] = n_det_per_module * n_modules;
    requisition->dims[1] = n_proj;
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
    return UFO_TASK_MODE_REDUCTOR;
}

static gboolean
ufo_rofex_reorder_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexReorderTaskPrivate *priv;
    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);

    // The generator-like behaviour is required for the case when
    // previous filter provided data for several frames.

    if (priv->frame_buf == NULL) {
        priv->frame_buf = ufo_buffer_dup(inputs[0]);
    }

    ufo_buffer_copy(inputs[0], priv->frame_buf);
    priv->generated = FALSE;
    priv->n_produced = 0;

    return FALSE;
}

static gboolean
ufo_rofex_reorder_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexReorderTaskPrivate *priv;
    priv = UFO_ROFEX_REORDER_TASK_GET_PRIVATE (task);

    if (!priv->generated) {
        UfoRequisition fb_req;
        ufo_buffer_get_requisition(priv->frame_buf, &fb_req);

        guint n_frames = (fb_req.n_dims == 3) ? fb_req.dims[2] : 1;
        gfloat *h_frame_buf = ufo_buffer_get_host_array(priv->frame_buf, NULL);
        gfloat *h_output = ufo_buffer_get_host_array(output, NULL);

        // Get frame and plane indices
        guint frame_index = priv->n_produced / priv->n_planes;
        guint plane_index = priv->n_produced % priv->n_planes;

        // Get pointer onto the frame in the frame buffer
        guint frame_size = fb_req.dims[0] * fb_req.dims[1];
        guint fb_offset = frame_index * frame_size;
        gfloat *h_frame = h_frame_buf + fb_offset;

        // Reorder data
        guint n_projections = fb_req.dims[0] / priv->n_det_per_module;

        reorder(h_frame,
                h_output,
                plane_index,
                priv->n_det_per_module,
                priv->n_modules,
                n_projections,
                priv->n_planes);

        // Set plane index
        GValue gv_plane_index = {0};
        g_value_init (&gv_plane_index, G_TYPE_UINT);
        g_value_set_uint (&gv_plane_index, plane_index);
        ufo_buffer_set_metadata(output, "plane-index", &gv_plane_index);

        //
        priv->n_produced++;
        if (priv->n_produced >= (n_frames * priv->n_planes)) {
            priv->n_produced = 0;
            priv->generated = TRUE;
        }

        return TRUE;
    }
    return FALSE;
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
        case PROP_N_MODULES:
            priv->n_modules = g_value_get_uint(value);
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
        case PROP_N_MODULES:
            g_value_set_uint (value, priv->n_modules);
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

    if (priv->frame_buf)
        g_object_unref(priv->frame_buf);

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
    iface->generate = ufo_rofex_reorder_task_generate;
}

static void
ufo_rofex_reorder_task_class_init (UfoRofexReorderTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_reorder_task_set_property;
    oclass->get_property = ufo_rofex_reorder_task_get_property;
    oclass->finalize = ufo_rofex_reorder_task_finalize;

    properties[PROP_N_MODULES] =
        g_param_spec_uint ("number-of-modules",
                           "The number of detector modules",
                           "The number of detector modules",
                           1, G_MAXUINT, 1,
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
    self->priv->n_modules = 1;
    self->priv->n_det_per_module = 1;
    self->priv->n_planes = 1;
}

static void
reorder(gfloat *input,
        gfloat *output,
        guint plane_index,
        guint n_dets,
        guint n_modules,
        guint n_proj,
        guint n_planes)
{
    /*
        Data inside the frame is ordered as following:
        ___ proj 1___  __ proj 2 __         __ proj K __
        [pix1 .. pixN] [pix1 .. pixN] .... [pix1 .. pixN]  | Plane 1  ||
        [pix1 .. pixN] [pix1 .. pixN] .... [pix1 .. pixN]  | Plane 2  ||
                                                                      || Mod 1
        [pix1 .. pixN] [pix1 .. pixN] .... [pix1 .. pixN]  | Plane M  ||


        [pix1 .. pixN] [pix1 .. pixN] .... [pix1 .. pixN]  | Plane 1  ||
        [pix1 .. pixN] [pix1 .. pixN] .... [pix1 .. pixN]  | Plane 2  ||
                                                                      || Mod R
        [pix1 .. pixN] [pix1 .. pixN] .... [pix1 .. pixN]  | Plane M  ||


        It has to be reordered as following:
         ____ Mod 1___   ____ Mod 2___    ____ Mod R__
        [pix1 .. pixN]  [pix1 .. pixN]   [pix1 .. pixN]    | Proj 1   ||
        [pix1 .. pixN]  [pix1 .. pixN]   [pix1 .. pixN]    | Proj 2   ||
                                                                      || Plane1
        [pix1 .. pixN]  [pix1 .. pixN]   [pix1 .. pixN]    | Proj K   ||

    */

    guint src_offset = 0;
    guint dst_offset = 0;

    for (guint module = 0; module < n_modules; module++) {
        for (guint proj = 0; proj < n_proj; proj++) {
            // Copy a block of detecto's pixels
            src_offset = proj * n_dets + plane_index * (n_proj * n_dets)
                       + module * (n_proj * n_dets * n_planes);

            dst_offset = module * n_dets + proj * (n_dets * n_modules);

            memcpy(output + dst_offset,
                   input + src_offset,
                   n_dets * sizeof(gfloat));

        }
    }
}
