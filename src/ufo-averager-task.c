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

#include <glib-object.h>
#include <gmodule.h>
#include <ufo-cpu-task-iface.h>
#include "ufo-averager-task.h"

/**
 * SECTION:ufo-averager-task
 * @Short_description: Write TIFF files
 * @Title: averager
 *
 * The averager node writes each incoming image as a TIFF using libtiff to disk.
 * Each file is prefixed with #UfoAveragerTask:prefix and written into
 * #UfoAveragerTask:path.
 */

struct _UfoAveragerTaskPrivate {
    guint counter;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoAveragerTask, ufo_averager_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_AVERAGER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_AVERAGER_TASK, UfoAveragerTaskPrivate))

UfoNode *
ufo_averager_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_AVERAGER_TASK, NULL));
}

static void
ufo_averager_task_setup (UfoTask *task,
                         UfoResources *resources,
                         GError **error)
{
}

static void
ufo_averager_task_get_requisition (UfoTask *task,
                                   UfoBuffer **inputs,
                                   UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static void
ufo_averager_task_get_structure (UfoTask *task,
                                 guint *n_inputs,
                                 UfoInputParam **in_params,
                                 UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_REDUCE;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
    (*in_params)[0].n_expected = -1;
}

static gboolean
ufo_averager_task_process (UfoCpuTask *task,
                           UfoBuffer **inputs,
                           UfoBuffer *output,
                           UfoRequisition *requisition)
{
    UfoAveragerTaskPrivate *priv;
    gfloat *in_array;
    gfloat *out_array;
    gsize n_pixels;

    priv = UFO_AVERAGER_TASK_GET_PRIVATE (UFO_AVERAGER_TASK (task));
    n_pixels = requisition->dims[0] * requisition->dims[1];
    in_array = ufo_buffer_get_host_array (inputs[0], NULL);
    out_array = ufo_buffer_get_host_array (output, NULL);

    for (gsize i = 0; i < n_pixels; i++)
        out_array[i] += in_array[i];

    priv->counter++;
    return TRUE;
}

static void
ufo_averager_task_reduce (UfoCpuTask *task,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoAveragerTaskPrivate *priv;
    gfloat *out_array;
    gsize n_pixels;

    priv = UFO_AVERAGER_TASK_GET_PRIVATE (UFO_AVERAGER_TASK (task));
    n_pixels = requisition->dims[0] * requisition->dims[1];
    out_array = ufo_buffer_get_host_array (output, NULL);

    for (gsize i = 0; i < n_pixels; i++)
        out_array[i] /= (gfloat) priv->counter;
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_averager_task_setup;
    iface->get_structure = ufo_averager_task_get_structure;
    iface->get_requisition = ufo_averager_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_averager_task_process;
    iface->reduce = ufo_averager_task_reduce;
}

static void
ufo_averager_task_class_init (UfoAveragerTaskClass *klass)
{
    g_type_class_add_private (G_OBJECT_CLASS (klass), sizeof(UfoAveragerTaskPrivate));
}

static void
ufo_averager_task_init(UfoAveragerTask *self)
{
    self->priv = UFO_AVERAGER_TASK_GET_PRIVATE(self);
    self->priv->counter = 0;
}
