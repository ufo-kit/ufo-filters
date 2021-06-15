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
#include "math.h"
#include <stdio.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-augmented-lamino-task.h"


struct _UfoAugmentedLaminoTaskPrivate {
    gfloat lamino_angle;
    guint number;
    guint slice_index;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoAugmentedLaminoTask, ufo_augmented_lamino_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_AUGMENTED_LAMINO_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_AUGMENTED_LAMINO_TASK, UfoAugmentedLaminoTaskPrivate))

enum {
    PROP_0,
    PROP_LAMINO_ANGLE,
    PROP_NUMBER,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_augmented_lamino_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_AUGMENTED_LAMINO_TASK, NULL));
}

static void
ufo_augmented_lamino_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoAugmentedLaminoTaskPrivate *priv = UFO_AUGMENTED_LAMINO_TASK_GET_PRIVATE (task);
    priv->slice_index = 0;
}

static void
ufo_augmented_lamino_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition,
                                 GError **error)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_augmented_lamino_task_get_num_inputs (UfoTask *task)
{
    return 2;
}

static guint
ufo_augmented_lamino_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_augmented_lamino_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR;
}

static gboolean
ufo_augmented_lamino_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoAugmentedLaminoTaskPrivate *priv = UFO_AUGMENTED_LAMINO_TASK_GET_PRIVATE(task);

    UfoRequisition in_req;
    ufo_buffer_get_requisition (inputs[0], &in_req);
    guint number_of_slices = priv->number;
    gint full_width = (gint) requisition->dims[0];
    gint full_height = (gint) requisition->dims[1];

    printf("width: %i\n", full_width);
    printf("height: %i\n", full_height);

    gfloat* in_tomo = ufo_buffer_get_host_array (inputs[0], NULL);
    gfloat* in_lamino = ufo_buffer_get_host_array (inputs[1], NULL);
    gfloat* out = ufo_buffer_get_host_array (output, NULL);
    guint current_slice = priv->slice_index;

    gfloat current_radius;

    if(current_slice < number_of_slices/2){
        current_radius = (float)current_slice * (gfloat)tan(priv->lamino_angle);
    }
    else{
        current_radius =  ((gfloat)tan(priv->lamino_angle) * (number_of_slices - 2) / 2.) -  (current_slice  - number_of_slices / 2.) * tan(priv->lamino_angle);
    }

    gboolean use_tomo;
    gint r_squared = current_radius * current_radius;
    for(gint y = 0; y < full_height; ++y){
        for(gint x=0; x < full_width/2; ++x){
            use_tomo = FALSE;

            if(x*x + y*y < r_squared){
                use_tomo = TRUE;
            }

            else if((x-full_width)*(x-full_width) + y*y < r_squared){
                use_tomo = TRUE;
            }
            else if(x*x + (y-full_height)*(y-full_height) < r_squared){
                use_tomo = TRUE;
            }
            else if((x-full_width)*(x-full_width) + (y-full_height)*(y-full_height) < r_squared){
                use_tomo = TRUE;
            }

            if(use_tomo){
                out[y * 2 * full_width + x * 2] = in_tomo[y * 2 * full_width + x * 2];
                out[y * 2 * full_width + x * 2 + 1] = in_tomo[y * 2 * full_width + x * 2 + 1];
            }
            else{
                out[y * 2 * full_width + x * 2] = in_lamino[y * 2 * full_width + x * 2];
                out[y * 2 * full_width + x * 2 + 1] = in_lamino[y * 2 * full_width + x * 2 + 1];
            }
        }
    }
    priv->slice_index++;
    return TRUE;
}


static void
ufo_augmented_lamino_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoAugmentedLaminoTaskPrivate *priv = UFO_AUGMENTED_LAMINO_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_LAMINO_ANGLE:
            priv->lamino_angle = g_value_get_float(value);
            break;
        case PROP_NUMBER:
            priv->number = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_augmented_lamino_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoAugmentedLaminoTaskPrivate *priv = UFO_AUGMENTED_LAMINO_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_LAMINO_ANGLE:
            g_value_set_float(value, priv->lamino_angle);
            break;
        case PROP_NUMBER:
            g_value_set_int(value, priv->number);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_augmented_lamino_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_augmented_lamino_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_augmented_lamino_task_setup;
    iface->get_num_inputs = ufo_augmented_lamino_task_get_num_inputs;
    iface->get_num_dimensions = ufo_augmented_lamino_task_get_num_dimensions;
    iface->get_mode = ufo_augmented_lamino_task_get_mode;
    iface->get_requisition = ufo_augmented_lamino_task_get_requisition;
    iface->process = ufo_augmented_lamino_task_process;
}

static void
ufo_augmented_lamino_task_class_init (UfoAugmentedLaminoTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_augmented_lamino_task_set_property;
    oclass->get_property = ufo_augmented_lamino_task_get_property;
    oclass->finalize = ufo_augmented_lamino_task_finalize;

    properties[PROP_LAMINO_ANGLE] =
        g_param_spec_float("lamino-angle",
            "Laminographic angle in radiant. Zero represents tomography",
            "Laminographic angle in radiant. Zero represents tomography",
            0.0,
            1.5707963267948966,
            0.5235987755982988,
            G_PARAM_READWRITE);

    properties[PROP_NUMBER] =
            g_param_spec_int("number",
                               "number of slices",
                               "number of slices",
                               1,
                               1000000,
                               2048,
                               G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoAugmentedLaminoTaskPrivate));
}

static void
ufo_augmented_lamino_task_init(UfoAugmentedLaminoTask *self)
{
    self->priv = UFO_AUGMENTED_LAMINO_TASK_GET_PRIVATE(self);
}
