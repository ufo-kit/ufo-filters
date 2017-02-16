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

#include <math.h>
#include <string.h>
#include "ufo-rofex-make-mask-task.h"

const gfloat OFFSET_DENOMINATOR = 360.0;

struct _UfoRofexMakeMaskTaskPrivate {
    guint number;
    guint current;

    guint n_modules;
    guint n_det_per_module;
    guint n_projections;
    gfloat source_offset;
    gfloat lower_limit_offset;
    gfloat upper_limit_offset;
    guint xa;
    guint xb;
    guint xc;
    guint xd;
    guint xe;
    guint xf;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexMakeMaskTask, ufo_rofex_make_mask_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_MAKE_MASK_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_MAKE_MASK_TASK, UfoRofexMakeMaskTaskPrivate))

enum {
    PROP_0,
    PROP_NUMBER,
    PROP_N_MODULES,
    PROP_N_DET_PER_MODULE,
    PROP_N_PROJECTIONS,
    PROP_SOURCE_OFFSET,
    PROP_LOWER_LIMIT_OFFSET,
    PROP_UPPER_LIMIT_OFFSET,
    PROP_XA,
    PROP_XB,
    PROP_XC,
    PROP_XD,
    PROP_XE,
    PROP_XF,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_make_mask_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_MAKE_MASK_TASK, NULL));
}

static void
ufo_rofex_make_mask_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
}

static void
ufo_rofex_make_mask_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexMakeMaskTaskPrivate *priv;
    priv = UFO_ROFEX_MAKE_MASK_TASK_GET_PRIVATE (task);

    requisition->n_dims = 2;
    requisition->dims[0] = priv->n_modules * priv->n_det_per_module;
    requisition->dims[1] = priv->n_projections;
}

static guint
ufo_rofex_make_mask_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_make_mask_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 3;
}

static UfoTaskMode
ufo_rofex_make_mask_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_GENERATOR | UFO_TASK_MODE_CPU;
}


static gboolean
ufo_rofex_make_mask_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexMakeMaskTaskPrivate *priv;
    priv = UFO_ROFEX_MAKE_MASK_TASK_GET_PRIVATE (task);

    if (priv->current == priv->number)
        return FALSE;

    guint n_proj = priv->n_projections;
    guint n_dets = priv->n_modules * priv->n_det_per_module;
    guint xa = priv->xa;
    guint xb = priv->xb;
    guint xc = priv->xc;
    guint xd = priv->xd;
    guint xe = priv->xe;
    guint xf = priv->xf;

    guint ya, yb, yc, yd, ye;
    guint y_max, y_min;

    gfloat lower_limit =
        (priv->lower_limit_offset + priv->source_offset) / OFFSET_DENOMINATOR;
    gfloat upper_limit =
        (priv->upper_limit_offset + priv->source_offset) / OFFSET_DENOMINATOR;

    gfloat *h_mask = ufo_buffer_get_host_array(output, NULL);
    guint mask_size = ufo_buffer_get_size(output);
    memset(h_mask, 1.0, mask_size);

    ya = round(lower_limit * n_proj);
    yb = ya;
    yc = round(upper_limit * n_proj);
    yd = yc;

    // slope of the straight
    gfloat m = ((gfloat)ya - (gfloat)yd) / ((gfloat)xa - (gfloat)xd);

    ye = round( (gfloat)yc + ((gfloat)xe - (gfloat)xc)*m );

    for (guint x = 0; x <= xa; x++) {
      y_min = ya;
      y_max = round(ye + m * x);
      for (guint y = y_min; y < y_max; y++)
          h_mask[x + y * n_dets] = 0.0;
    }

    for (guint x = xa; x <= xc; x++) {
      y_min = round(ya + m * (x - xa));
      y_max = round(ye + m * x);
      for (guint y = y_min; y < y_max; y++)
          h_mask[x + y * n_dets] = 0.0;
    }

    for (guint x = xc; x <= xd; x++) {
      y_min = round(ya + m * (x - xa));
      y_max = yd;
      for (guint y = y_min; y < y_max; y++)
          h_mask[x + y * n_dets] = 0.0;
    }

    for (guint x = xb; x <= xf; x++) {
      y_min = yb;
      y_max = round(yb + m * (x - xb));
      for (guint y = y_min; y < y_max; y++)
          h_mask[x + y * n_dets] = 0.0;
    }

    guint nvals = lower_limit * n_dets * n_proj;
    memset(h_mask, 0.0, nvals * sizeof(gfloat));

    nvals = upper_limit * n_dets * n_proj;
    memset(h_mask + nvals, 0.0, mask_size - nvals * sizeof(gfloat));

    priv->current++;

    return TRUE;
}

static void
ufo_rofex_make_mask_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexMakeMaskTaskPrivate *priv = UFO_ROFEX_MAKE_MASK_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_NUMBER:
            priv->number = g_value_get_uint(value);
            break;
        case PROP_N_MODULES:
            priv->n_modules = g_value_get_uint(value);
            break;
        case PROP_N_DET_PER_MODULE:
            priv->n_det_per_module = g_value_get_uint(value);
            break;
        case PROP_N_PROJECTIONS:
            priv->n_projections = g_value_get_uint(value);
            break;
        case PROP_SOURCE_OFFSET:
            priv->source_offset = g_value_get_float(value);
            break;
        case PROP_LOWER_LIMIT_OFFSET:
            priv->lower_limit_offset = g_value_get_float(value);
            break;
        case PROP_UPPER_LIMIT_OFFSET:
            priv->upper_limit_offset = g_value_get_float(value);
            break;
        case PROP_XA:
            priv->xa = g_value_get_uint(value);
            break;
        case PROP_XB:
            priv->xb = g_value_get_uint(value);
            break;
        case PROP_XC:
            priv->xc = g_value_get_uint(value);
            break;
        case PROP_XD:
            priv->xd = g_value_get_uint(value);
            break;
        case PROP_XE:
            priv->xe = g_value_get_uint(value);
            break;
        case PROP_XF:
            priv->xf = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_make_mask_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexMakeMaskTaskPrivate *priv = UFO_ROFEX_MAKE_MASK_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_NUMBER:
            g_value_set_uint (value, priv->number);
            break;
        case PROP_N_MODULES:
            g_value_set_uint (value, priv->n_modules);
            break;
        case PROP_N_DET_PER_MODULE:
            g_value_set_uint (value, priv->n_det_per_module);
            break;
        case PROP_N_PROJECTIONS:
            g_value_set_uint (value, priv->n_projections);
            break;
        case PROP_SOURCE_OFFSET:
            g_value_set_float(value, priv->source_offset);
            break;
        case PROP_LOWER_LIMIT_OFFSET:
            g_value_set_float(value, priv->lower_limit_offset);
            break;
        case PROP_UPPER_LIMIT_OFFSET:
            g_value_set_float(value, priv->upper_limit_offset);
            break;
        case PROP_XA:
            g_value_set_uint(value, priv->xa);
            break;
        case PROP_XB:
            g_value_set_uint(value, priv->xb);
            break;
        case PROP_XC:
            g_value_set_uint(value, priv->xc);
            break;
        case PROP_XD:
            g_value_set_uint(value, priv->xd);
            break;
        case PROP_XE:
            g_value_set_uint(value, priv->xe);
            break;
        case PROP_XF:
            g_value_set_uint(value, priv->xf);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_make_mask_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_make_mask_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_make_mask_task_setup;
    iface->get_num_inputs = ufo_rofex_make_mask_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_make_mask_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_make_mask_task_get_mode;
    iface->get_requisition = ufo_rofex_make_mask_task_get_requisition;
    iface->generate = ufo_rofex_make_mask_task_generate;
}

static void
ufo_rofex_make_mask_task_class_init (UfoRofexMakeMaskTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_make_mask_task_set_property;
    oclass->get_property = ufo_rofex_make_mask_task_get_property;
    oclass->finalize = ufo_rofex_make_mask_task_finalize;

    properties[PROP_NUMBER] =
        g_param_spec_uint ("number",
                           "Number of masks",
                           "Number of masks",
                           1, 2 << 16, 1,
                           G_PARAM_READWRITE);

    properties[PROP_N_MODULES] =
        g_param_spec_uint ("number-of-modules",
                           "The number of detector modules",
                           "The number of detector modules",
                           1, G_MAXUINT, 27,
                           G_PARAM_READWRITE);

    properties[PROP_N_DET_PER_MODULE] =
               g_param_spec_uint ("number-of-detectors-per-module",
                                  "The number of pixels per detector module",
                                  "The number of pixels per detector module",
                                  1, G_MAXUINT, 16,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PROJECTIONS] =
               g_param_spec_uint ("number-of-projections",
                                  "The number of fan-beam projections",
                                  "The number of fan-beam projections",
                                  1, G_MAXUINT, 180,
                                  G_PARAM_READWRITE);

    properties[PROP_SOURCE_OFFSET] =
              g_param_spec_float ("source-offset",
                                  "Source offset.",
                                  "Source offset.",
                                  G_MINFLOAT, G_MAXFLOAT, 23.2,
                                  G_PARAM_READWRITE);

    properties[PROP_LOWER_LIMIT_OFFSET] =
              g_param_spec_float ("lower-limit-offset",
                                  "The lower limit for offset.",
                                  "The lower limit for offset.",
                                  G_MINFLOAT, G_MAXFLOAT, 47.0,
                                  G_PARAM_READWRITE);

    properties[PROP_UPPER_LIMIT_OFFSET] =
              g_param_spec_float ("upper-limit-offset",
                                  "The upper limit for offset.",
                                  "The upper limit for offset.",
                                  G_MINFLOAT, G_MAXFLOAT, 313.0,
                                  G_PARAM_READWRITE);

    properties[PROP_XA] =
              g_param_spec_uint ("xa",
                                 "X-offset to point A",
                                 "X-offset to point A",
                                 0, G_MAXUINT, 43,
                                 G_PARAM_READWRITE);

    properties[PROP_XB] =
              g_param_spec_uint ("xb",
                                 "X-offset to point B",
                                 "X-offset to point B",
                                 0, G_MAXUINT, 285,
                                 G_PARAM_READWRITE);

    properties[PROP_XC] =
              g_param_spec_uint ("xc",
                                 "X-offset to point C",
                                 "X-offset to point C",
                                 0, G_MAXUINT, 175,
                                 G_PARAM_READWRITE);

    properties[PROP_XD] =
              g_param_spec_uint ("xd",
                                 "X-offset to point D",
                                 "X-offset to point D",
                                 0, G_MAXUINT, 362,
                                 G_PARAM_READWRITE);

    properties[PROP_XE] =
              g_param_spec_uint ("xe",
                                 "X-offset to point E",
                                 "X-offset to point E",
                                 0, G_MAXUINT, 0,
                                 G_PARAM_READWRITE);

    properties[PROP_XF] =
              g_param_spec_uint ("xf",
                                 "X-offset to point F",
                                 "X-offset to point F",
                                 0, G_MAXUINT, 431,
                                 G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexMakeMaskTaskPrivate));
}

static void
ufo_rofex_make_mask_task_init(UfoRofexMakeMaskTask *self)
{
    self->priv = UFO_ROFEX_MAKE_MASK_TASK_GET_PRIVATE(self);
    self->priv->number = 1;
    self->priv->n_modules = 27;
    self->priv->n_det_per_module = 16;
    self->priv->n_projections = 180;
    self->priv->source_offset = 23.2;
    self->priv->lower_limit_offset = 47.0;
    self->priv->upper_limit_offset = 313.0;
    self->priv->xa = 43;
    self->priv->xb = 285;
    self->priv->xc = 175;
    self->priv->xd = 362;
    self->priv->xe = 0;
    self->priv->xf = 431;
}
