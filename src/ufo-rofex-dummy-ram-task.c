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
#include "ufo-rofex-dummy-ram-task.h"


struct _UfoRofexDummyRamTaskPrivate {
    guint n_modules;
    guint n_planes;
    guint n_frames;
    gboolean collect_frames;

    guint current_module;
    guint current_plane;
    guint current_frame;
    UfoBuffer **modules_buf;

    gboolean generated;
    gboolean stop_processing;
    guint generated_modules;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexDummyRamTask, ufo_rofex_dummy_ram_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_DUMMY_RAM_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_DUMMY_RAM_TASK, UfoRofexDummyRamTaskPrivate))

enum {
    PROP_0,
    PROP_N_MODULES,
    PROP_N_PLANES,
    PROP_N_FRAMES,
    PROP_COLLECT_FRAMES,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_dummy_ram_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_DUMMY_RAM_TASK, NULL));
}

static void
ufo_rofex_dummy_ram_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexDummyRamTaskPrivate *priv;
    priv = UFO_ROFEX_DUMMY_RAM_TASK_GET_PRIVATE (task);

    priv->current_module = 0;
    priv->current_plane = 0;
    priv->current_frame = 0;
    priv->modules_buf = NULL;

    priv->generated = FALSE;
    priv->stop_processing = FALSE;
    priv->generated_modules = 0;
}

static void
ufo_rofex_dummy_ram_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexDummyRamTaskPrivate *priv;
    priv = UFO_ROFEX_DUMMY_RAM_TASK_GET_PRIVATE (task);

    UfoRequisition in_req;
    ufo_buffer_get_requisition(inputs[0], &in_req);

    guint n_det_per_module = in_req.dims[0];
    guint n_projections = in_req.dims[1];

    requisition->n_dims = 2;
    requisition->dims[0] = n_det_per_module * n_projections;
    requisition->dims[1] = priv->n_planes * priv->n_frames;
}

static guint
ufo_rofex_dummy_ram_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_dummy_ram_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_rofex_dummy_ram_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR;
}

static gboolean
ufo_rofex_dummy_ram_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    // Got (n_det_per_module x n_projections) data from the detector module
    // Collect planes for each detector, next push it one chunk by one

    UfoRofexDummyRamTaskPrivate *priv;
    priv = UFO_ROFEX_DUMMY_RAM_TASK_GET_PRIVATE (task);

    if ( priv->stop_processing ) {
        return FALSE;
    }

    UfoRequisition in_req;
    ufo_buffer_get_requisition(inputs[0], &in_req);

    guint n_det_per_module = in_req.dims[0];
    guint n_projections = in_req.dims[1];
    guint n_planes = priv->n_planes;

    if ( ! priv->modules_buf ) {
        // Allocate buffers per module if not allocated
        guint n_frames = priv->collect_frames ? priv->n_frames : 1;
        guint n_modules = priv->n_modules;

        UfoRequisition buf_req;
        buf_req.n_dims = 2;
        buf_req.dims[0] = n_det_per_module;
        buf_req.dims[1] = n_projections * n_planes * n_frames;

        priv->modules_buf = g_malloc( n_modules * sizeof(UfoBuffer*) );
        for (guint module_ind = 0; module_ind < n_modules; module_ind++) {
            priv->modules_buf[module_ind] = ufo_buffer_new(&buf_req, NULL);
        }
    }

    // We assume that data from the detectors comes by a round robin fashion.
    // Each module sends data measured at a single plane. When all modules sent
    // the data. After that, data for the next plane are sent. When all planes
    // passed, it starts next frame.

    // Copy data into the module's buffer
    gfloat *h_input;
    gfloat *h_module;

    h_input = ufo_buffer_get_host_array(inputs[0], NULL);
    h_module = ufo_buffer_get_host_array(priv->modules_buf[priv->current_module], NULL);
    guint dst_offset = (n_det_per_module * n_projections) * priv->current_plane;
    if ( priv->collect_frames ) {
          dst_offset += (n_det_per_module * n_projections * n_planes) * priv->current_frame;
    }
    guint size = ufo_buffer_get_size(inputs[0]);

    memcpy(h_module + dst_offset, h_input, size);

    // Counting and logic
    priv->current_module++;

    if ( priv->current_module >= priv->n_modules ) {
        // all modules for the current plane generated a data chunk,
        // now do next plane
        priv->current_module = 0;
        priv->current_plane++;
    }

    if ( priv->current_plane >= priv->n_planes ) {
        // all planes for current frame were processed
        // now do next frame
        priv->current_plane = 0;
        priv->current_frame++;

        // Return FALSE to generate a data per frame.
        if ( ! priv->collect_frames ) {
            priv->generated = FALSE;
            return FALSE;
        }
    }

    // Return FALSE to generate a data that includes all frames
    if ( priv->collect_frames && priv->current_frame >= priv->n_frames ) {
        priv->generated = FALSE;
        priv->stop_processing = TRUE;
        priv->current_frame = 0;
        return FALSE;
    }

    return TRUE;
}

static gboolean
ufo_rofex_dummy_ram_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexDummyRamTaskPrivate *priv;
    priv = UFO_ROFEX_DUMMY_RAM_TASK_GET_PRIVATE (task);

    if (!priv->generated) {
        UfoBuffer *src = priv->modules_buf[priv->generated_modules];
        ufo_buffer_copy(src, output);

        // increase n generated
        priv->generated_modules++;
        //g_printf("RAM. generated module: %d of %d frames: %d\n", priv->generated_modules, priv->n_modules,  priv->current_frame);
        if (priv->generated_modules >= priv->n_modules) {
            priv->generated_modules = 0;
            priv->generated = TRUE;
        }
        return TRUE;
    }
    return FALSE;
}

static void
ufo_rofex_dummy_ram_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexDummyRamTaskPrivate *priv;
    priv = UFO_ROFEX_DUMMY_RAM_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_MODULES:
            priv->n_modules = g_value_get_uint(value);
            break;
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
            break;
        case PROP_N_FRAMES:
            priv->n_frames = g_value_get_uint(value);
            break;
        case PROP_COLLECT_FRAMES:
            priv->collect_frames = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_dummy_ram_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexDummyRamTaskPrivate *priv = UFO_ROFEX_DUMMY_RAM_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_MODULES:
            g_value_set_uint (value, priv->n_modules);
            break;
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
            break;
        case PROP_N_FRAMES:
            g_value_set_uint (value, priv->n_frames);
            break;
        case PROP_COLLECT_FRAMES:
            g_value_set_boolean (value, priv->collect_frames);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_dummy_ram_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_dummy_ram_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_dummy_ram_task_setup;
    iface->get_num_inputs = ufo_rofex_dummy_ram_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_dummy_ram_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_dummy_ram_task_get_mode;
    iface->get_requisition = ufo_rofex_dummy_ram_task_get_requisition;
    iface->process = ufo_rofex_dummy_ram_task_process;
    iface->generate = ufo_rofex_dummy_ram_task_generate;
}

static void
ufo_rofex_dummy_ram_task_class_init (UfoRofexDummyRamTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_dummy_ram_task_set_property;
    oclass->get_property = ufo_rofex_dummy_ram_task_get_property;
    oclass->finalize = ufo_rofex_dummy_ram_task_finalize;

    properties[PROP_N_MODULES] =
        g_param_spec_uint ("number-of-modules",
                           "The number of detector modules",
                           "The number of detector modules",
                           1, G_MAXUINT, 1,
                           G_PARAM_READWRITE);

    properties[PROP_N_PLANES] =
               g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    properties[PROP_N_FRAMES] =
                g_param_spec_uint ("number-of-frames",
                                   "The number of frames",
                                   "The number of frames made by a detector module",
                                   1, G_MAXUINT, 1,
                                   G_PARAM_READWRITE);
    properties[PROP_COLLECT_FRAMES] =
            g_param_spec_boolean ("collect-frames",
                                  "Collect all frames before pass forward.",
                                  "Collect all frames before pass forward.",
                                  FALSE,
                                  G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexDummyRamTaskPrivate));
}

static void
ufo_rofex_dummy_ram_task_init(UfoRofexDummyRamTask *self)
{
    self->priv = UFO_ROFEX_DUMMY_RAM_TASK_GET_PRIVATE(self);
    self->priv->n_modules = 1;
    self->priv->n_planes = 1;
    self->priv->n_frames = 1;
    self->priv->collect_frames = FALSE;
}
