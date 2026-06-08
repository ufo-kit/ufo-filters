/*
 * Copyright (C) 2026 Karlsruhe Institute of Technology
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

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-compand-task.h"

typedef enum {
    COMPAND_DIRECTION_FORWARD,
    COMPAND_DIRECTION_BACKWARD,
    COMPAND_N_DIRECTIONS
} CompandDirection;

typedef enum {
    COMPAND_TYPE_TANH,
    COMPAND_TYPE_ARCTAN,
    COMPAND_TYPE_RECIP_SQRT,
    COMPAND_TYPE_CLIP,
    COMPAND_N_TYPES
} CompandType;

static GEnumValue direction_values[] = {
    { COMPAND_DIRECTION_FORWARD, "COMPAND_DIRECTION_FORWARD", "forward" },
    { COMPAND_DIRECTION_BACKWARD, "COMPAND_DIRECTION_BACKWARD", "backward" },
    { 0, NULL, NULL }
};

static GEnumValue type_values[] = {
    { COMPAND_TYPE_TANH, "COMPAND_TYPE_TANH", "tanh" },
    { COMPAND_TYPE_ARCTAN, "COMPAND_TYPE_ARCTAN", "arctan" },
    { COMPAND_TYPE_RECIP_SQRT, "COMPAND_TYPE_RECIP_SQRT", "recip_sqrt" },
    { COMPAND_TYPE_CLIP, "COMPAND_TYPE_CLIP", "clip" },
    { 0, NULL, NULL }
};

static const gchar *kernel_names[COMPAND_N_TYPES][COMPAND_N_DIRECTIONS] = {
    { "tanh_compand_forward", "tanh_compand_backward" },
    { "arctan_compand_forward", "arctan_compand_backward" },
    { "recip_sqrt_compand_forward", "recip_sqrt_compand_backward" },
    { "clip_compand_forward", "clip_compand_backward" }
};

struct _UfoCompandTaskPrivate {
    cl_kernel kernels[COMPAND_N_TYPES][COMPAND_N_DIRECTIONS];

    gfloat center;
    gfloat delta;
    guint bits;
    CompandDirection direction;
    CompandType type;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoCompandTask, ufo_compand_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_COMPAND_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_COMPAND_TASK, UfoCompandTaskPrivate))

enum {
    PROP_0,
    PROP_CENTER,
    PROP_DELTA,
    PROP_BITS,
    PROP_DIRECTION,
    PROP_TYPE,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_compand_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_COMPAND_TASK, NULL));
}

static void
ufo_compand_task_setup (UfoTask *task,
                        UfoResources *resources,
                        GError **error)
{
    UfoCompandTaskPrivate *priv;

    priv = UFO_COMPAND_TASK_GET_PRIVATE (task);

    for (guint type = 0; type < COMPAND_N_TYPES; type++) {
        for (guint direction = 0; direction < COMPAND_N_DIRECTIONS; direction++) {
            priv->kernels[type][direction] = ufo_resources_get_kernel (resources,
                                                                       "compand.cl",
                                                                       kernel_names[type][direction],
                                                                       NULL,
                                                                       error);

            if (priv->kernels[type][direction])
                UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->kernels[type][direction]), error);
        }
    }
}

static void
ufo_compand_task_get_requisition (UfoTask *task,
                                  UfoBuffer **inputs,
                                  UfoRequisition *requisition,
                                  GError **error)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_compand_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_compand_task_get_num_dimensions (UfoTask *task,
                                     guint input)
{
    g_return_val_if_fail (input == 0, 0);

    return 2;
}

static UfoTaskMode
ufo_compand_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_compand_task_process (UfoTask *task,
                          UfoBuffer **inputs,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoCompandTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_kernel kernel;
    gfloat dynamic_range;

    priv = UFO_COMPAND_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    kernel = priv->kernels[priv->type][priv->direction];
    dynamic_range = (gfloat) ((1 << priv->bits) - 1);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 2, sizeof (gfloat), &priv->center));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 3, sizeof (gfloat), &priv->delta));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 4, sizeof (gfloat), &dynamic_range));

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, kernel, 2, requisition->dims, NULL);

    return TRUE;
}

static void
ufo_compand_task_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
    UfoCompandTaskPrivate *priv;

    priv = UFO_COMPAND_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_CENTER:
            priv->center = g_value_get_float (value);
            break;
        case PROP_DELTA:
            priv->delta = g_value_get_float (value);
            break;
        case PROP_BITS:
            {
                guint bits = g_value_get_uint (value);

                if (bits != 8 && bits != 16) {
                    g_warning ("Compand::bits can only be 8 or 16");
                    return;
                }

                priv->bits = bits;
            }
            break;
        case PROP_DIRECTION:
            priv->direction = g_value_get_enum (value);
            break;
        case PROP_TYPE:
            priv->type = g_value_get_enum (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_compand_task_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
    UfoCompandTaskPrivate *priv;

    priv = UFO_COMPAND_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_CENTER:
            g_value_set_float (value, priv->center);
            break;
        case PROP_DELTA:
            g_value_set_float (value, priv->delta);
            break;
        case PROP_BITS:
            g_value_set_uint (value, priv->bits);
            break;
        case PROP_DIRECTION:
            g_value_set_enum (value, priv->direction);
            break;
        case PROP_TYPE:
            g_value_set_enum (value, priv->type);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_compand_task_finalize (GObject *object)
{
    UfoCompandTaskPrivate *priv;

    priv = UFO_COMPAND_TASK_GET_PRIVATE (object);

    for (guint type = 0; type < COMPAND_N_TYPES; type++) {
        for (guint direction = 0; direction < COMPAND_N_DIRECTIONS; direction++) {
            if (priv->kernels[type][direction]) {
                UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernels[type][direction]));
                priv->kernels[type][direction] = NULL;
            }
        }
    }

    G_OBJECT_CLASS (ufo_compand_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_compand_task_setup;
    iface->get_num_inputs = ufo_compand_task_get_num_inputs;
    iface->get_num_dimensions = ufo_compand_task_get_num_dimensions;
    iface->get_mode = ufo_compand_task_get_mode;
    iface->get_requisition = ufo_compand_task_get_requisition;
    iface->process = ufo_compand_task_process;
}

static void
ufo_compand_task_class_init (UfoCompandTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_compand_task_set_property;
    oclass->get_property = ufo_compand_task_get_property;
    oclass->finalize = ufo_compand_task_finalize;

    properties[PROP_CENTER] =
        g_param_spec_float ("center",
            "Center",
            "Center value of the compander",
            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
            G_PARAM_READWRITE);

    properties[PROP_DELTA] =
        g_param_spec_float ("delta",
            "Delta",
            "Grey value spacing",
            0.0f, G_MAXFLOAT, 1.0f,
            G_PARAM_READWRITE);

    properties[PROP_BITS] =
        g_param_spec_uint ("bits",
            "Bits",
            "Output bit depth. Possible values are 8 and 16.",
            8, 16, 16,
            G_PARAM_READWRITE);

    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction",
            "Direction",
            "Companding direction: \"forward\", \"backward\"",
            g_enum_register_static ("ufo_compand_direction", direction_values),
            COMPAND_DIRECTION_FORWARD,
            G_PARAM_READWRITE);

    properties[PROP_TYPE] =
        g_param_spec_enum ("type",
            "Type",
            "Compander type: \"tanh\", \"arctan\", \"recip_sqrt\", \"clip\"",
            g_enum_register_static ("ufo_compand_type", type_values),
            COMPAND_TYPE_CLIP,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoCompandTaskPrivate));
}

static void
ufo_compand_task_init (UfoCompandTask *self)
{
    self->priv = UFO_COMPAND_TASK_GET_PRIVATE (self);

    self->priv->center = 0.0f;
    self->priv->delta = 1.0f;
    self->priv->bits = 16;
    self->priv->direction = COMPAND_DIRECTION_FORWARD;
    self->priv->type = COMPAND_TYPE_CLIP;
}
