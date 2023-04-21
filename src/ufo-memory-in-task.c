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
#include "config.h"

#include "ufo-memory-in-task.h"

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif


typedef enum {
    UFO_MEMORY_IN_LOCATION_HOST,
    UFO_MEMORY_IN_LOCATION_BUFFER,
} MemoryLocation;

static GEnumValue memory_location_values[] = {
    { UFO_MEMORY_IN_LOCATION_HOST,      "UFO_MEMORY_IN_LOCATION_HOST",      "host" },
    { UFO_MEMORY_IN_LOCATION_BUFFER,    "UFO_MEMORY_IN_LOCATION_BUFFER",    "buffer" },
    { 0, NULL, NULL}
};

struct _UfoMemoryInTaskPrivate {
    guint8 *pointer;
    guint   width;
    guint   height;
    gsize     bytes_per_pixel;
    UfoBufferDepth   bitdepth;
    guint   number;
    guint   read;
    gboolean complex_layout;
    MemoryLocation mem_in_location;
    cl_context context;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoMemoryInTask, ufo_memory_in_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_MEMORY_IN_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_MEMORY_IN_TASK, UfoMemoryInTaskPrivate))

enum {
    PROP_0,
    PROP_POINTER,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_BITDEPTH,
    PROP_NUMBER,
    PROP_COMPLEX_LAYOUT,
    PROP_MEMORY_LOCATION,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_memory_in_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_MEMORY_IN_TASK, NULL));
}

static void
ufo_memory_in_task_setup (UfoTask *task,
                          UfoResources *resources,
                          GError **error)
{
    UfoMemoryInTaskPrivate *priv;

    priv = UFO_MEMORY_IN_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainContext (priv->context), error);

    if (priv->pointer == NULL)
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, "`pointer' property not set");

    priv->read = 0;
}

static void
ufo_memory_in_task_get_requisition (UfoTask *task,
                                    UfoBuffer **inputs,
                                    UfoRequisition *requisition,
                                    GError **error)
{
    UfoMemoryInTaskPrivate *priv;

    priv = UFO_MEMORY_IN_TASK_GET_PRIVATE (task);
    requisition->n_dims = 2;
    requisition->dims[0] = priv->width;
    requisition->dims[1] = priv->height;
}

static guint
ufo_memory_in_task_get_num_inputs (UfoTask *task)
{
    return 0;
}

static guint
ufo_memory_in_task_get_num_dimensions (UfoTask *task,
                                       guint input)
{
    return 0;
}

static UfoTaskMode
ufo_memory_in_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_GENERATOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_memory_in_task_generate (UfoTask *task,
                             UfoBuffer *output,
                             UfoRequisition *requisition)
{
    UfoMemoryInTaskPrivate *priv;
    guint8 *data;
    gsize size;

    priv = UFO_MEMORY_IN_TASK_GET_PRIVATE (task);
    size = priv->width * priv->height * priv->bytes_per_pixel;

    if (priv->read == priv->number)
        return FALSE;

    switch (priv->mem_in_location) {
        case UFO_MEMORY_IN_LOCATION_HOST:
            {
                data = (guint8 *) ufo_buffer_get_host_array (output, NULL);
                memcpy (
                    data,
                    priv->pointer + (priv->read * priv->width * priv->height * priv->bytes_per_pixel),
                    priv->width * priv->height * priv->bytes_per_pixel
                );
            }
            break;
        case UFO_MEMORY_IN_LOCATION_BUFFER:
            {
                cl_command_queue cmd_queue;
                UfoGpuNode *node;
                cl_int errcode;
                cl_mem src_mem = (cl_mem) priv->pointer;
                cl_context src_context;
                gsize src_size;

                node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
                cmd_queue = ufo_gpu_node_get_cmd_queue (node);
                cl_mem dst_mem = ufo_buffer_get_device_array (output, cmd_queue);
                /* Check context */
                UFO_RESOURCES_CHECK_CLERR (
                    clGetMemObjectInfo (
                        src_mem,
                        CL_MEM_CONTEXT,
                        sizeof (cl_context),
                        &src_context,
                        NULL
                    )
                );
                if (priv->context != src_context) {
                    g_error ("Input context does not match UFO context");
                    return FALSE;
                }
                /* Check size */
                UFO_RESOURCES_CHECK_CLERR (
                    clGetMemObjectInfo (
                        src_mem,
                        CL_MEM_SIZE,
                        sizeof (gsize),
                        &src_size,
                        NULL
                    )
                );
                if (src_size != ufo_buffer_get_size (output)) {
                    g_error ("Input has wrong size");
                    return FALSE;
                }

                errcode = clEnqueueCopyBuffer (cmd_queue,
                                               src_mem,
                                               dst_mem,
                                               0, 0,
                                               size,
                                               0, NULL, NULL);

                UFO_RESOURCES_CHECK_CLERR (errcode);
            }
            break;
        default:
            g_error ("Unknown memory-location");
    }

    if (priv->bitdepth != UFO_BUFFER_DEPTH_32F)
        ufo_buffer_convert (output, priv->bitdepth);

    if (priv->complex_layout) {
        ufo_buffer_set_layout (output, UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED);
    }
    priv->read++;

    return TRUE;
}

static void
ufo_memory_in_task_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
    UfoMemoryInTaskPrivate *priv = UFO_MEMORY_IN_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_POINTER:
            priv->pointer = (gpointer) g_value_get_ulong (value);
            break;
        case PROP_WIDTH:
            priv->width = g_value_get_uint (value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint (value);
            break;
        case PROP_NUMBER:
            priv->number = g_value_get_uint (value);
            break;
        case PROP_BITDEPTH:
            switch(g_value_get_uint(value)){
                case 8:
                    priv->bitdepth = UFO_BUFFER_DEPTH_8U;
                    priv->bytes_per_pixel = 1;
                    break;
                case 16:
                    priv->bitdepth = UFO_BUFFER_DEPTH_16U;
                    priv->bytes_per_pixel = 2;
                    break;
                case 32:
                    priv->bitdepth = UFO_BUFFER_DEPTH_32F;
                    priv->bytes_per_pixel = 4;
                    break;
                default:
                    g_warning("Cannot set bitdepth other than 8, 16, 32.");
            }
            break;
        case PROP_COMPLEX_LAYOUT:
            priv->complex_layout = g_value_get_boolean (value);
            break;
        case PROP_MEMORY_LOCATION:
            priv->mem_in_location = g_value_get_enum (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_memory_in_task_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
    UfoMemoryInTaskPrivate *priv = UFO_MEMORY_IN_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_POINTER:
            g_value_set_ulong (value, (gulong) priv->pointer);
            break;
        case PROP_WIDTH:
            g_value_set_uint (value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint (value, priv->height);
            break;
        case PROP_BITDEPTH:
            g_value_set_uint (value, priv->bitdepth);
            break;
        case PROP_NUMBER:
            g_value_set_uint (value, priv->number);
            break;
        case PROP_COMPLEX_LAYOUT:
            g_value_set_boolean (value, priv->complex_layout);
            break;
        case PROP_MEMORY_LOCATION:
            g_value_set_enum (value, priv->mem_in_location);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_memory_in_task_finalize (GObject *object)
{
    UfoMemoryInTaskPrivate *priv = UFO_MEMORY_IN_TASK_GET_PRIVATE (object);

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }
    G_OBJECT_CLASS (ufo_memory_in_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_memory_in_task_setup;
    iface->get_num_inputs = ufo_memory_in_task_get_num_inputs;
    iface->get_num_dimensions = ufo_memory_in_task_get_num_dimensions;
    iface->get_mode = ufo_memory_in_task_get_mode;
    iface->get_requisition = ufo_memory_in_task_get_requisition;
    iface->generate = ufo_memory_in_task_generate;
}

static void
ufo_memory_in_task_class_init (UfoMemoryInTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_memory_in_task_set_property;
    oclass->get_property = ufo_memory_in_task_get_property;
    oclass->finalize = ufo_memory_in_task_finalize;

    properties[PROP_POINTER] =
        g_param_spec_ulong ("pointer",
            "Pointer to pre-allocated memory",
            "Pointer to pre-allocated memory",
            0, G_MAXULONG, 0,
            G_PARAM_READWRITE);

    properties[PROP_WIDTH] =
        g_param_spec_uint ("width",
            "Width of the buffer",
            "Width of the buffer",
            1, 2 << 16, 1,
            G_PARAM_READWRITE);

    properties[PROP_HEIGHT] =
        g_param_spec_uint ("height",
            "Height of the buffer",
            "Height of the buffer",
            1, 2 << 16, 1,
            G_PARAM_READWRITE);

    properties[PROP_BITDEPTH] =
        g_param_spec_uint("bitdepth",
            "Bitdepth of the buffer",
            "Bitdepth of the buffer",
            0, G_MAXUINT, G_MAXUINT,
            G_PARAM_READWRITE);

    properties[PROP_NUMBER] =
        g_param_spec_uint ("number",
            "Number of buffers",
            "Number of buffers",
            1, 2 << 16, 1,
            G_PARAM_READWRITE);

    properties[PROP_COMPLEX_LAYOUT] =
        g_param_spec_boolean("complex-layout",
            "Treat input as interleaved complex64 data type (x[0] = Re(z[0]), x[1] = Im(z[0]), ...)",
            "Treat input as interleaved complex64 data type (x[0] = Re(z[0]), x[1] = Im(z[0]), ...)",
            FALSE,
            G_PARAM_READWRITE);

    properties[PROP_MEMORY_LOCATION] =
        g_param_spec_enum ("memory-location",
            "Location of the input memory (\"host\", \"buffer\")",
            "Location of the input memory (\"host\", \"buffer\")",
            g_enum_register_static ("ufo_memory_in_location_values", memory_location_values),
            UFO_MEMORY_IN_LOCATION_HOST, G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoMemoryInTaskPrivate));
}

static void
ufo_memory_in_task_init(UfoMemoryInTask *self)
{
    self->priv = UFO_MEMORY_IN_TASK_GET_PRIVATE(self);
    self->priv->pointer = NULL;
    self->priv->width = 1;
    self->priv->height = 1;
    self->priv->bitdepth = UFO_BUFFER_DEPTH_32F;
    self->priv->bytes_per_pixel = 4;
    self->priv->number = 0;
    self->priv->complex_layout = FALSE;
    self->priv->mem_in_location = UFO_MEMORY_IN_LOCATION_HOST;
}
