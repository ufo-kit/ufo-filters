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
#include "ufo-rofex-frame-buffer-task.h"


struct _UfoRofexFrameBufferTaskPrivate {
    // Rofex configuration
    guint n_modules;
    guint n_det_per_module;
    guint n_planes;
    // Define how much frames are produced per generation
    guint portion_size;

    //
    guint orig_portion_size;
    guint n_processed;
    guint n_produced;
    gboolean generated;

    guint     *frame_counters;
    UfoBuffer *frame_buf;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexFrameBufferTask, ufo_rofex_frame_buffer_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_FRAME_BUFFER_TASK, UfoRofexFrameBufferTaskPrivate))

enum {
    PROP_0,
    PROP_N_MODULES,
    PROP_N_DET_PER_MODULE,
    PROP_N_PLANES,
    PROP_PORTION_SIZE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_frame_buffer_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_FRAME_BUFFER_TASK, NULL));
}

static void
ufo_rofex_frame_buffer_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexFrameBufferTaskPrivate *priv;
    priv = UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE (task);

    priv->orig_portion_size = priv->portion_size;
    priv->n_processed = 0;
    priv->n_produced = 0;
    priv->generated = TRUE;
    priv->frame_buf = NULL;
    priv->frame_counters = g_malloc0(priv->n_modules * sizeof(guint));
}

static void
ufo_rofex_frame_buffer_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexFrameBufferTaskPrivate *priv;
    priv = UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE (task);

    UfoRequisition det_data_req;
    ufo_buffer_get_requisition(inputs[0], &det_data_req);

    guint n_dets = priv->n_det_per_module;
    guint n_proj = det_data_req.dims[0] / n_dets;

    requisition->n_dims  = 2;
    requisition->dims[0] = n_proj * n_dets;
    requisition->dims[1] = priv->n_modules * priv->n_planes;

    if (priv->portion_size > 1) {
        requisition->n_dims = 3;
        requisition->dims[2] = priv->portion_size;
    }
}

static guint
ufo_rofex_frame_buffer_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_frame_buffer_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_rofex_frame_buffer_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR;
}

static gboolean
ufo_rofex_frame_buffer_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexFrameBufferTaskPrivate *priv;
    priv = UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE (task);

    UfoRequisition det_data_req;
    ufo_buffer_get_requisition(inputs[0], &det_data_req);
    guint in_frames = det_data_req.dims[1] / priv->n_planes;

    // g_warning("ufo_rofex_frame_buffer_task_process RECIVED: %d frames",
    //            in_frames);

    // -- Allocate frame buffer
    if (priv->frame_buf == NULL) {
        // Allocate frame_buf. It is required even when no buffering
        // required. Since detectors can send data for multiple frames.

        UfoRequisition fb_req;
        fb_req.n_dims = 3;
        fb_req.dims[0] = requisition->dims[0];
        fb_req.dims[1] = requisition->dims[1];
        if (priv->portion_size > in_frames) {
            fb_req.dims[2] = priv->portion_size;
        } else {
            fb_req.dims[2] = in_frames;
        }

        priv->frame_buf = ufo_buffer_new(&fb_req, NULL);
    }

    // -- Identify the module
    guint  module_index = 0;
    GValue *gv_module_index = NULL;
    gv_module_index = ufo_buffer_get_metadata(inputs[0], "module-index");

    if (gv_module_index) {
        module_index = g_value_get_uint (gv_module_index);
    } else {
        g_info("Module index is not specified. Use round robin.");
        module_index = priv->n_processed % priv->n_modules;
    }

    // -- Calculate offsets in frame_buf
    guint n_dets = priv->n_det_per_module;
    guint n_proj = det_data_req.dims[0] / n_dets;

    guint in_frame_offset = 0;
    guint per_frame_offset = 0;

    in_frame_offset = module_index * (priv->n_planes * n_proj * n_dets);
    per_frame_offset = priv->n_modules * (priv->n_planes * n_proj * n_dets);

    // -- Copy data to frame_buf
    guint n_frames = priv->frame_counters[module_index];
    gfloat *h_frame_buf = ufo_buffer_get_host_array(priv->frame_buf, NULL);
    gfloat *h_det_data_buf = ufo_buffer_get_host_array(inputs[0], NULL);

    guint fb_offset = 0;
    guint input_offset = 0;

    for (guint i = 0; i < in_frames; i++) {
        fb_offset = in_frame_offset + (n_frames + i) * per_frame_offset;
        input_offset = i * (priv->n_planes * n_proj * n_dets);

        memcpy(h_frame_buf + fb_offset,
               h_det_data_buf + input_offset,
               (priv->n_planes * n_proj * n_dets) * sizeof(gfloat));
    }

    // -- Increase counters
    priv->frame_counters[module_index] += in_frames;
    priv->n_processed++;

    gboolean collected = TRUE;
    for (guint i = 0; i < priv->n_modules; i++) {
        collected = collected && priv->frame_counters[i] >= priv->portion_size;
    }

    if (priv->n_processed == priv->n_modules && collected) {
        priv->n_processed = 0;
        priv->n_produced = 0;
        priv->generated = FALSE;
        return FALSE;
    }

    if (priv->n_processed == priv->n_modules) {
        priv->n_processed = 0;
    }

    return TRUE;
}

static gboolean
ufo_rofex_frame_buffer_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexFrameBufferTaskPrivate *priv;
    priv = UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE (task);

    if (!priv->generated) {
        // -- Buffer size
        UfoRequisition frame_buf_req;
        ufo_buffer_get_requisition(priv->frame_buf, &frame_buf_req);
        guint n_frames = frame_buf_req.dims[2];
        guint frame_size = frame_buf_req.dims[0] * frame_buf_req.dims[1];

        // -- Copy frames
        gfloat *h_output = ufo_buffer_get_host_array(output, NULL);
        gfloat *h_frame_buf = ufo_buffer_get_host_array(priv->frame_buf, NULL);

        guint frame_buf_offset = priv->n_produced * frame_size;
        memcpy(h_output,
               h_frame_buf + frame_buf_offset,
               priv->portion_size * frame_size * sizeof(gfloat));

        priv->n_produced += priv->portion_size;
        g_warning("ufo_rofex_frame_buffer_task_generate: %d of %d : p size %d", priv->n_produced, n_frames, priv->portion_size);

        if (priv->n_produced >= n_frames) {
            priv->generated = TRUE;
            priv->portion_size = priv->orig_portion_size;

            // clear counters
            for (guint i = 0; i < priv->n_modules; i++) {
                priv->frame_counters[i] = 0;
            }
        }

        guint frames_gap = n_frames - priv->n_produced;
        if ( frames_gap > 0 && frames_gap < priv->portion_size) {
            // The amount of data not enought to make a portion
            // reduce the portion size, so the last portion will have
            // only a part of data
            g_warning("The amount of frames is not enough to produce the next portion.");
            priv->portion_size = frames_gap;
        }

        return TRUE;
    }

    return FALSE;
}

static void
ufo_rofex_frame_buffer_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFrameBufferTaskPrivate *priv;
    priv = UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE (object);

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
        case PROP_PORTION_SIZE:
            priv->portion_size = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_frame_buffer_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFrameBufferTaskPrivate *priv;
    priv = UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE (object);

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
        case PROP_PORTION_SIZE:
            g_value_set_uint (value, priv->portion_size);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_frame_buffer_task_finalize (GObject *object)
{
    UfoRofexFrameBufferTaskPrivate *priv;
    priv = UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE (object);

    if (priv->frame_counters)
        g_free(priv->frame_counters);

    if (priv->frame_buf)
        g_object_unref(priv->frame_buf);

    G_OBJECT_CLASS (ufo_rofex_frame_buffer_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_frame_buffer_task_setup;
    iface->get_num_inputs = ufo_rofex_frame_buffer_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_frame_buffer_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_frame_buffer_task_get_mode;
    iface->get_requisition = ufo_rofex_frame_buffer_task_get_requisition;
    iface->process = ufo_rofex_frame_buffer_task_process;
    iface->generate = ufo_rofex_frame_buffer_task_generate;
}

static void
ufo_rofex_frame_buffer_task_class_init (UfoRofexFrameBufferTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_frame_buffer_task_set_property;
    oclass->get_property = ufo_rofex_frame_buffer_task_get_property;
    oclass->finalize = ufo_rofex_frame_buffer_task_finalize;

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

    properties[PROP_PORTION_SIZE] =
                g_param_spec_uint ("portion-size",
                                   "The number of frames per generation",
                                   "The number of frames per generation",
                                   1, G_MAXUINT, 1,
                                   G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexFrameBufferTaskPrivate));
}

static void
ufo_rofex_frame_buffer_task_init(UfoRofexFrameBufferTask *self)
{
    self->priv = UFO_ROFEX_FRAME_BUFFER_TASK_GET_PRIVATE(self);
    self->priv->n_modules = 1;
    self->priv->n_det_per_module = 1;
    self->priv->n_planes = 1;
    self->priv->portion_size = 1;
}
