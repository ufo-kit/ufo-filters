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
#include "ufo-null-task.h"

/**
 * SECTION:ufo-null-task
 * @Short_description: Eat input
 * @Title: null
 */

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoNullTask, ufo_null_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_NULL_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_NULL_TASK, UfoNullTaskPrivate))

UfoNode *
ufo_null_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_NULL_TASK, NULL));
}

static void
ufo_null_task_setup (UfoTask *task,
                     UfoResources *resources,
                     GError **error)
{
}

static void
ufo_null_task_get_requisition (UfoTask *task,
                               UfoBuffer **inputs,
                               UfoRequisition *requisition)
{
    requisition->n_dims = 0;
}

static void
ufo_null_task_get_structure (UfoTask *task,
                             guint *n_inputs,
                             UfoInputParam **in_params,
                             UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
    (*in_params)[0].n_expected = -1;
}

static gboolean
ufo_null_task_process (UfoCpuTask *task,
                       UfoBuffer **inputs,
                       UfoBuffer *output,
                       UfoRequisition *requisition)
{
    return TRUE;
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_null_task_setup;
    iface->get_structure = ufo_null_task_get_structure;
    iface->get_requisition = ufo_null_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_null_task_process;
}

static void
ufo_null_task_class_init (UfoNullTaskClass *klass)
{
}

static void
ufo_null_task_init(UfoNullTask *self)
{
}
