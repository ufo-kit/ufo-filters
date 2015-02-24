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

#include "ufo-priv.h"
#include "ufo-monitor-task.h"


static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoMonitorTask, ufo_monitor_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_MONITOR_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_MONITOR_TASK, UfoMonitorTaskPrivate))


UfoNode *
ufo_monitor_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_MONITOR_TASK, NULL));
}

static void
ufo_monitor_task_setup (UfoTask *task,
                        UfoResources *resources,
                        GError **error)
{
}

static void
ufo_monitor_task_get_requisition (UfoTask *task,
                                  UfoBuffer **inputs,
                                  UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_monitor_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_monitor_task_get_num_dimensions (UfoTask *task,
                                     guint input)
{
    return 2;
}

static UfoTaskMode
ufo_monitor_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR;
}

static gchar *
join_list (GList *list, const gchar *sep)
{
    gchar **array;
    GList *it;
    gchar *result;
    guint i = 0;

    array = g_new0 (gchar *, g_list_length (list) + 1);

    g_list_for (list, it)
        array[i++] = it->data;

    result = g_strjoinv (sep, array);
    g_free (array);
    return result;
}

static gboolean
ufo_monitor_task_process (UfoTask *task,
                          UfoBuffer **inputs,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoBufferLocation location;
    GList *keys;
    GList *sizes;
    gchar *keystring;
    gchar *dimstring;

    location = ufo_buffer_get_location (inputs[0]);
    keys = ufo_buffer_get_metadata_keys (inputs[0]);
    sizes = NULL;

    for (guint i = 0; i < requisition->n_dims; i++)
        sizes = g_list_append (sizes, g_strdup_printf ("%zu", requisition->dims[i]));

    dimstring = join_list (sizes, " ");
    keystring = join_list (keys, ", ");

    g_print ("monitor: dims=[%s] keys=[%s] location=", dimstring, keystring);

    switch (location) {
        case UFO_BUFFER_LOCATION_HOST:
            g_print ("host");
            break;
        case UFO_BUFFER_LOCATION_DEVICE:
            g_print ("device");
            break;
        case UFO_BUFFER_LOCATION_DEVICE_IMAGE:
            g_print ("image");
            break;
        case UFO_BUFFER_LOCATION_INVALID:
            g_print ("invalid");
            break;
    }

    g_print ("\n");
    ufo_buffer_copy (inputs[0], output);

    g_free (dimstring);
    g_free (keystring);
    g_list_free (keys);
    g_list_free_full (sizes, (GDestroyNotify) g_free);

    return TRUE;
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_monitor_task_setup;
    iface->get_num_inputs = ufo_monitor_task_get_num_inputs;
    iface->get_num_dimensions = ufo_monitor_task_get_num_dimensions;
    iface->get_mode = ufo_monitor_task_get_mode;
    iface->get_requisition = ufo_monitor_task_get_requisition;
    iface->process = ufo_monitor_task_process;
}

static void
ufo_monitor_task_class_init (UfoMonitorTaskClass *klass)
{
}

static void
ufo_monitor_task_init(UfoMonitorTask *self)
{
}
