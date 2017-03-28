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

#include "ufo-rofex-group-slices-task.h"

/*
  Description:
  The filter handles a sequence of images, which relate to the different planes
  and frames, it groups them in the stacks (one per plane) and arranges
  according to the frame index.

  Input:
  A 2D image of the following dimensions:
    0: width
    1: height

  Output:
  A series of stacks of 2D images:
    0: width
    1: height
    2: nFrames
*/


struct _UfoRofexGroupSlicesTaskPrivate {
    guint n_frames;
    guint n_planes;

    gfloat **planes_buffers;
    guint *frames_couters;
    gboolean generated;
    guint n_images;
    guint image;
    guint plane;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexGroupSlicesTask, ufo_rofex_group_slices_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_GROUP_SLICES_TASK, UfoRofexGroupSlicesTaskPrivate))

enum {
    PROP_0,
    PROP_N_FRAMES,
    PROP_N_PLANES,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_group_slices_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_GROUP_SLICES_TASK, NULL));
}

static void
ufo_rofex_group_slices_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexGroupSlicesTaskPrivate *priv;
    priv = UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE (task);

    priv->planes_buffers = NULL;
    priv->frames_couters = NULL;

    priv->n_images = priv->n_planes * priv->n_frames;
    priv->generated = FALSE;
    priv->image = 0;
    priv->plane = 0;
}

static void
ufo_rofex_group_slices_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexGroupSlicesTaskPrivate *priv;
    priv = UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], requisition);

    requisition->n_dims = 3;
    requisition->dims[2] = priv->n_frames;
}

static guint
ufo_rofex_group_slices_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_group_slices_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_group_slices_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_rofex_group_slices_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexGroupSlicesTaskPrivate *priv;
    UfoRequisition req;
    GValue *gv_index = NULL;
    guint plane_index, frame_index;
    gfloat *src;
    gfloat *dst;
    gsize size, frame_size, offset;

    priv = UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition(inputs[0], &req);

    if (!priv->planes_buffers) {
        priv->planes_buffers = (gfloat **)g_malloc0(priv->n_planes * sizeof(gfloat*));
        priv->frames_couters = (guint*) g_malloc0(priv->n_planes * sizeof(guint));

        size = req.dims[0] * req.dims[1] * priv->n_frames * sizeof(gfloat);
        for (guint plane = 0; plane < priv->n_planes; plane++) {
            priv->planes_buffers[plane] = (gfloat*) g_malloc0(size);
        }
    }

    frame_size = req.dims[0] * req.dims[1];
    gv_index = ufo_buffer_get_metadata(inputs[0], "plane-index");
    plane_index = gv_index ? g_value_get_uint (gv_index) : 0;
    frame_index = priv->frames_couters[plane_index];

    src = ufo_buffer_get_host_array (inputs[0], NULL);
    dst = priv->planes_buffers[plane_index];
    offset = frame_index * frame_size;

    size = frame_size * sizeof(gfloat);
    memcpy (dst + offset, src, size);
    priv->frames_couters[plane_index] += 1;
    priv->image++;

    if (priv->image == priv->n_images) {
        priv->image = 0;
        priv->generated = FALSE;
        return FALSE;
    }

    return TRUE;
}

static gboolean
ufo_rofex_group_slices_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexGroupSlicesTaskPrivate *priv;
    priv = UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE (task);

    if (!priv->generated) {
        gfloat *src;
        gfloat *dst;
        gsize size;

        src = priv->planes_buffers[priv->plane];
        dst = ufo_buffer_get_host_array (output, NULL);
        size = ufo_buffer_get_size(output);
        memcpy (dst, src, size);

        priv->plane++;
        if (priv->plane >= priv->n_planes) {
            priv->plane = 0;
            priv->generated = TRUE;
        }

        return TRUE;
    }
    return FALSE;
}

static void
ufo_rofex_group_slices_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexGroupSlicesTaskPrivate *priv;
    priv = UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_FRAMES:
            priv->n_frames = g_value_get_uint(value);
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
ufo_rofex_group_slices_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexGroupSlicesTaskPrivate *priv;
    priv = UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_FRAMES:
            g_value_set_uint (value, priv->n_frames);
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
ufo_rofex_group_slices_task_finalize (GObject *object)
{
    UfoRofexGroupSlicesTaskPrivate *priv;
    priv = UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE (object);

    if (priv->planes_buffers) {
        for (guint plane = 0; plane < priv->n_planes; plane++) {
            g_free (priv->planes_buffers[plane]);
            priv->planes_buffers[plane] = NULL;
        }
        g_free (priv->planes_buffers);
        priv->planes_buffers = NULL;
    }

    if (priv->frames_couters) {
        g_free (priv->frames_couters);
        priv->frames_couters = NULL;
    }

    G_OBJECT_CLASS (ufo_rofex_group_slices_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_group_slices_task_setup;
    iface->get_num_inputs = ufo_rofex_group_slices_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_group_slices_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_group_slices_task_get_mode;
    iface->get_requisition = ufo_rofex_group_slices_task_get_requisition;
    iface->process = ufo_rofex_group_slices_task_process;
    iface->generate = ufo_rofex_group_slices_task_generate;
}

static void
ufo_rofex_group_slices_task_class_init (UfoRofexGroupSlicesTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_group_slices_task_set_property;
    oclass->get_property = ufo_rofex_group_slices_task_get_property;
    oclass->finalize = ufo_rofex_group_slices_task_finalize;

    properties[PROP_N_FRAMES] =
                g_param_spec_uint ("number-of-frames",
                                  "The number of frames",
                                  "The number of frames",
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

    g_type_class_add_private (oclass, sizeof(UfoRofexGroupSlicesTaskPrivate));
}

static void
ufo_rofex_group_slices_task_init(UfoRofexGroupSlicesTask *self)
{
    self->priv = UFO_ROFEX_GROUP_SLICES_TASK_GET_PRIVATE(self);
    self->priv->n_frames = 1;
    self->priv->n_planes = 1;
}
