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

#include <stdio.h>
#include "ufo-stdout-task.h"
#include "writers/ufo-writer.h"


struct _UfoStdoutTaskPrivate {
    UfoBufferDepth depth;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoStdoutTask, ufo_stdout_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_STDOUT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_STDOUT_TASK, UfoStdoutTaskPrivate))

enum {
    PROP_0,
    PROP_BITS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_stdout_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_STDOUT_TASK, NULL));
}

static void
ufo_stdout_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_stdout_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    requisition->n_dims = 0;
}

static guint
ufo_stdout_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_stdout_task_get_num_dimensions (UfoTask *task,
                                    guint input)
{
    return 2;
}

static UfoTaskMode
ufo_stdout_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_SINK | UFO_TASK_MODE_CPU;
}

static gboolean
ufo_stdout_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoStdoutTaskPrivate *priv;
    UfoWriterImage image;
    UfoRequisition in;
    gpointer data;
    gsize size;

    priv = UFO_STDOUT_TASK_GET_PRIVATE (task);
    data = ufo_buffer_get_host_array (inputs[0], NULL);
    size = priv->depth == UFO_BUFFER_DEPTH_8U ? 1 : 
           ((priv->depth == UFO_BUFFER_DEPTH_16U || priv->depth == UFO_BUFFER_DEPTH_16S) ? 2 : 4);

    ufo_buffer_get_requisition (inputs[0], &in);

    for (guint i = 0; i < in.n_dims; i++)
        size *= in.dims[i];

    image.data = data;
    image.requisition = &in;
    image.depth = priv->depth;

    ufo_writer_convert_inplace (&image);
    fwrite (data, size, 1, stdout);
    fflush (stdout);

    return TRUE;
}

static void
ufo_stdout_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoStdoutTaskPrivate *priv = UFO_STDOUT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_BITS:
            {
                guint val = g_value_get_uint (value);

                if (val != 8 && val != 16 && val != 32) {
                    g_warning ("::bits can only be 8, 16 or 32");
                    return;
                }

                if (val == 8)
                    priv->depth = UFO_BUFFER_DEPTH_8U;

                if (val == 16)
                    priv->depth = UFO_BUFFER_DEPTH_16U;

                if (val == 32)
                    priv->depth = UFO_BUFFER_DEPTH_32F;
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_stdout_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoStdoutTaskPrivate *priv = UFO_STDOUT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_BITS:
            if (priv->depth == UFO_BUFFER_DEPTH_8U)
                g_value_set_uint (value, 8);

            if (priv->depth == UFO_BUFFER_DEPTH_16U)
                g_value_set_uint (value, 16);

            if (priv->depth == UFO_BUFFER_DEPTH_32F)
                g_value_set_uint (value, 32);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_stdout_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_stdout_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_stdout_task_setup;
    iface->get_num_inputs = ufo_stdout_task_get_num_inputs;
    iface->get_num_dimensions = ufo_stdout_task_get_num_dimensions;
    iface->get_mode = ufo_stdout_task_get_mode;
    iface->get_requisition = ufo_stdout_task_get_requisition;
    iface->process = ufo_stdout_task_process;
}

static void
ufo_stdout_task_class_init (UfoStdoutTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_stdout_task_set_property;
    oclass->get_property = ufo_stdout_task_get_property;
    oclass->finalize = ufo_stdout_task_finalize;

    properties[PROP_BITS] =
        g_param_spec_uint ("bits",
                           "Number of bits per sample",
                           "Number of bits per sample. Possible values in [8, 16, 32].",
                           8, 32, 32, G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoStdoutTaskPrivate));
}

static void
ufo_stdout_task_init(UfoStdoutTask *self)
{
    self->priv = UFO_STDOUT_TASK_GET_PRIVATE(self);
    self->priv->depth = UFO_BUFFER_DEPTH_32F;
}
