/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
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

#include <string.h>
#include "ufo-buffer-task.h"

/**
 * SECTION:ufo-buffer-task
 * @Short_description: Write TIFF files
 * @Title: buffer
 *
 */

struct _UfoBufferTaskPrivate {
    guchar *data;
    gsize n_elements;
    gsize current_element;
    gsize size;
    gsize current_size;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoBufferTask, ufo_buffer_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_BUFFER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_BUFFER_TASK, UfoBufferTaskPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

/* static GParamSpec *properties[N_PROPERTIES] = { NULL, }; */

UfoNode *
ufo_buffer_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_BUFFER_TASK, NULL));
}

static void
ufo_buffer_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_buffer_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoBufferTaskPrivate *priv;

    priv = UFO_BUFFER_TASK_GET_PRIVATE (task);
    priv->size = ufo_buffer_get_size (inputs[0]);
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static void
ufo_buffer_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               UfoInputParam **in_params,
                               UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_GENERATE;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
}

static gboolean
ufo_buffer_task_process (UfoCpuTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoBufferTaskPrivate *priv;

    priv = UFO_BUFFER_TASK_GET_PRIVATE (task);

    if (priv->data == NULL) {
        priv->current_size = 4 * priv->size;
        priv->data = g_malloc0 (priv->current_size);
    }

    if (priv->current_size <= priv->n_elements * priv->size) {
        priv->current_size *= 2;
        priv->data = g_realloc (priv->data, priv->current_size);
    }

    g_memmove (priv->data + priv->n_elements * priv->size,
               ufo_buffer_get_host_array (inputs[0], NULL),
               priv->size);

    priv->n_elements++;
    return TRUE;
}

static gboolean
ufo_buffer_task_generate (UfoCpuTask *task,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoBufferTaskPrivate *priv;

    priv = UFO_BUFFER_TASK_GET_PRIVATE (task);

    if (priv->current_element == priv->n_elements)
        return FALSE;

    g_memmove (ufo_buffer_get_host_array (output, NULL),
               priv->data + priv->current_element * priv->size,
               priv->size);

    priv->current_element++;
    return TRUE;
}

static void
ufo_buffer_task_finalize (GObject *object)
{
    UfoBufferTaskPrivate *priv;

    priv = UFO_BUFFER_TASK_GET_PRIVATE (object);

    if (priv->data != NULL) {
        g_free (priv->data);
        priv->data = NULL;
    }

    G_OBJECT_CLASS (ufo_buffer_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_buffer_task_setup;
    iface->get_structure = ufo_buffer_task_get_structure;
    iface->get_requisition = ufo_buffer_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_buffer_task_process;
    iface->generate = ufo_buffer_task_generate;
}

static void
ufo_buffer_task_class_init (UfoBufferTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = ufo_buffer_task_finalize;
    g_type_class_add_private (gobject_class, sizeof(UfoBufferTaskPrivate));
}

static void
ufo_buffer_task_init(UfoBufferTask *self)
{
    self->priv = UFO_BUFFER_TASK_GET_PRIVATE(self);
    self->priv->data = NULL;
    self->priv->n_elements = 0;
    self->priv->current_element = 0;
}
