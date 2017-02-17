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
#include "ufo-rofex-fan2para-params-task.h"


static void
compute_fan2par_transp (UfoTask *task,
                        UfoBuffer *params_buf);

struct _UfoRofexFan2paraParamsTaskPrivate {
    guint  n_modules;
    guint  n_det_per_module;
    guint  n_proj;
    guint  n_planes;

    guint  n_par_proj;
    guint  n_par_dets;
    gfloat source_offset;

    gfloat *source_angle;
    guint  source_angle_len;

    gfloat *source_diameter;
    guint  source_diameter_len;

    gfloat *delta_x;
    guint  delta_x_len;

    gfloat *delta_z;
    guint  delta_z_len;

    gfloat detector_diameter;
    gfloat image_width;
    gfloat image_center_x;
    gfloat image_center_y;

    gboolean generated;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexFan2paraParamsTask, ufo_rofex_fan2para_params_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_FAN2PARA_PARAMS_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_FAN2PARA_PARAMS_TASK, UfoRofexFan2paraParamsTaskPrivate))

enum {
    PROP_0,
    PROP_N_MODULES,
    PROP_N_DET_PER_MODULE,
    PROP_N_PROJECTIONS,
    PROP_N_PLANES,
    PROP_N_PAR_DETECTORS,
    PROP_N_PAR_PROJECTIONS,
    PROP_SOURCE_OFFSET,
    PROP_SOURCE_ANGLE,
    PROP_SOURCE_DIAMETER,
    PROP_DELTA_X,
    PROP_DELTA_Z,
    PROP_DETECTOR_DIAMETER,
    PROP_IMAGE_WIDTH,
    PROP_IMAGE_CENTER_X,
    PROP_IMAGE_CENTER_Y,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_rofex_fan2para_params_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_FAN2PARA_PARAMS_TASK, NULL));
}

static void
ufo_rofex_fan2para_params_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexFan2paraParamsTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PARA_PARAMS_TASK_GET_PRIVATE (task);
    priv->generated = FALSE;
}

static void
ufo_rofex_fan2para_params_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexFan2paraParamsTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PARA_PARAMS_TASK_GET_PRIVATE (task);

    /*  This filter calculates 18 different parameters.
        14 params, each of (n_par_dets * n_par_proj * n_planes) values.
        1 param, of n_par_dets values
        1 param, of n_par_proj values
        2 params, of n_proj values
    */

    requisition->n_dims = 2;
    requisition->dims[1] = 18;
    requisition->dims[0] = priv->n_par_dets * priv->n_par_proj * priv->n_planes;

    // just to be sure that we have enough memory
    if (requisition->dims[0] < priv->n_proj) {
        requisition->dims[0] = priv->n_proj;
    }
}

static guint
ufo_rofex_fan2para_params_task_get_num_inputs (UfoTask *task)
{
    return 0;
}

static guint
ufo_rofex_fan2para_params_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 0;
}

static UfoTaskMode
ufo_rofex_fan2para_params_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_GENERATOR;
}

static gboolean
ufo_rofex_fan2para_params_task_generate (UfoTask *task,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexFan2paraParamsTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PARA_PARAMS_TASK_GET_PRIVATE (task);

    if (!priv->generated){
        compute_fan2par_transp(task, output);
        priv->generated = TRUE;
        return TRUE;
    }

    return FALSE;
}

static void
ufo_rofex_fan2para_params_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFan2paraParamsTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PARA_PARAMS_TASK_GET_PRIVATE (object);
    gchar **angles_str = NULL;

    switch (property_id) {
        case PROP_N_MODULES:
            priv->n_modules = g_value_get_uint(value);
            break;
        case PROP_N_DET_PER_MODULE:
            priv->n_det_per_module = g_value_get_uint(value);
            break;
        case PROP_N_PROJECTIONS:
            priv->n_proj = g_value_get_uint(value);
            break;
        case PROP_N_PLANES:
            priv->n_planes = g_value_get_uint(value);
            break;
        case PROP_N_PAR_DETECTORS:
            priv->n_par_dets = g_value_get_uint(value);
            break;
        case PROP_N_PAR_PROJECTIONS:
            priv->n_par_proj = g_value_get_uint(value);
            break;
        case PROP_SOURCE_OFFSET:
            priv->source_offset = g_value_get_float(value);
            break;
        case PROP_SOURCE_ANGLE:
            angles_str = g_strsplit(g_value_get_string(value), " ", -1);
            g_warning("N: %lu", sizeof(angles_str) / sizeof(gchar*));
            g_strfreev(angles_str);
            break;
        case PROP_SOURCE_DIAMETER:
            break;
        case PROP_DELTA_X:
            break;
        case PROP_DELTA_Z:
            break;
        case PROP_DETECTOR_DIAMETER:
            priv->detector_diameter = g_value_get_float(value);
            break;
        case PROP_IMAGE_WIDTH:
            priv->image_width = g_value_get_float(value);
            break;
        case PROP_IMAGE_CENTER_X:
            priv->image_center_x = g_value_get_float(value);
            break;
        case PROP_IMAGE_CENTER_Y:
            priv->image_center_y = g_value_get_float(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_fan2para_params_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFan2paraParamsTaskPrivate *priv = UFO_ROFEX_FAN2PARA_PARAMS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_N_MODULES:
            g_value_set_uint (value, priv->n_modules);
            break;
        case PROP_N_DET_PER_MODULE:
            g_value_set_uint (value, priv->n_det_per_module);
            break;
        case PROP_N_PROJECTIONS:
            g_value_set_uint (value, priv->n_proj);
            break;
        case PROP_N_PLANES:
            g_value_set_uint (value, priv->n_planes);
            break;
        case PROP_N_PAR_DETECTORS:
            g_value_set_uint(value, priv->n_par_dets);
            break;
        case PROP_N_PAR_PROJECTIONS:
            g_value_set_uint(value, priv->n_par_proj);
            break;
        case PROP_SOURCE_OFFSET:
            g_value_set_float(value, priv->source_offset);
            break;
        case PROP_SOURCE_ANGLE:
            break;
        case PROP_SOURCE_DIAMETER:
            break;
        case PROP_DELTA_X:
            break;
        case PROP_DELTA_Z:
            break;
        case PROP_DETECTOR_DIAMETER:
            g_value_set_float(value, priv->detector_diameter);
            break;
        case PROP_IMAGE_WIDTH:
            g_value_set_float(value, priv->image_width);
            break;
        case PROP_IMAGE_CENTER_X:
            g_value_set_float(value, priv->image_center_x);
            break;
        case PROP_IMAGE_CENTER_Y:
            g_value_set_float(value, priv->image_center_y);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_rofex_fan2para_params_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_rofex_fan2para_params_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_fan2para_params_task_setup;
    iface->get_num_inputs = ufo_rofex_fan2para_params_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_fan2para_params_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_fan2para_params_task_get_mode;
    iface->get_requisition = ufo_rofex_fan2para_params_task_get_requisition;
    iface->generate = ufo_rofex_fan2para_params_task_generate;
}

static void
ufo_rofex_fan2para_params_task_class_init (UfoRofexFan2paraParamsTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_fan2para_params_task_set_property;
    oclass->get_property = ufo_rofex_fan2para_params_task_get_property;
    oclass->finalize = ufo_rofex_fan2para_params_task_finalize;

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

    properties[PROP_N_PLANES] =
               g_param_spec_uint ("number-of-planes",
                                  "The number of planes",
                                  "The number of planes",
                                  1, G_MAXUINT, 1,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PAR_DETECTORS] =
               g_param_spec_uint ("number-of-parallel-detectors",
                                  "The number of pixels in a parallel projection",
                                  "The number of pixels in a parallel projection",
                                  1, G_MAXUINT, 256,
                                  G_PARAM_READWRITE);

    properties[PROP_N_PAR_PROJECTIONS] =
               g_param_spec_uint ("number-of-parallel-projections",
                                  "The number of parallel projection",
                                  "The number of parallel projection",
                                  1, G_MAXUINT, 512,
                                  G_PARAM_READWRITE);

    properties[PROP_SOURCE_OFFSET] =
                g_param_spec_float ("source-offset",
                                    "Source offset.",
                                    "Source offset.",
                                    G_MINFLOAT, G_MAXFLOAT, 23.2,
                                    G_PARAM_READWRITE);

    properties[PROP_SOURCE_ANGLE] =
                g_param_spec_string ("source-angle",
                                    "Angles of the source separated by the space.",
                                    "Angles of the source separated by the space.",
                                    "240.0 240.0",
                                    G_PARAM_READWRITE);

    properties[PROP_SOURCE_DIAMETER] =
                g_param_spec_string ("source-diameter",
                                    "Diameters of the source separated by the space.",
                                    "Diameters of the source separated by the space.",
                                    "265.0 270.0",
                                    G_PARAM_READWRITE);

    properties[PROP_DELTA_X] =
                g_param_spec_string ("delta-x",
                                    "Delta X, separated the space.",
                                    "Delta X, separated the space.",
                                    "815.0 815.0",
                                    G_PARAM_READWRITE);

    properties[PROP_DELTA_Z] =
                g_param_spec_string ("delta-z",
                                     "Delta Z, separated the space.",
                                     "Delta Z, separated the space.",
                                     "1417.0 1430.0",
                                     G_PARAM_READWRITE);

    properties[PROP_DETECTOR_DIAMETER] =
                g_param_spec_float ("detector-diameter",
                                    "Detector diameter.",
                                    "Detector diameter.",
                                    0, G_MAXFLOAT, 216.0,
                                    G_PARAM_READWRITE);

    properties[PROP_IMAGE_WIDTH] =
                g_param_spec_float ("image-width",
                                    "Image width.",
                                    "Image width.",
                                    0, G_MAXFLOAT, 190.0,
                                    G_PARAM_READWRITE);

    properties[PROP_IMAGE_CENTER_X] =
                g_param_spec_float ("image-center-x",
                                    "Image center X.",
                                    "Image center X.",
                                    0, G_MAXFLOAT, 0.0,
                                    G_PARAM_READWRITE);

    properties[PROP_IMAGE_CENTER_Y] =
                g_param_spec_float ("image-center-y",
                                    "Image center Y.",
                                    "Image center Y.",
                                    0, G_MAXFLOAT, 0.0,
                                    G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoRofexFan2paraParamsTaskPrivate));
}

static void
ufo_rofex_fan2para_params_task_init(UfoRofexFan2paraParamsTask *self)
{
    self->priv = UFO_ROFEX_FAN2PARA_PARAMS_TASK_GET_PRIVATE(self);
    self->priv->n_modules = 27;
    self->priv->n_det_per_module = 16;
    self->priv->n_proj = 180;
    self->priv->n_planes = 1;
    self->priv->n_par_dets = 256;
    self->priv->n_par_proj = 512;
    self->priv->source_offset = 23.2;
    self->priv->source_angle = NULL;
    self->priv->source_angle_len = 0;
    self->priv->source_diameter = NULL;
    self->priv->source_diameter_len = 0;
    self->priv->delta_x = NULL;
    self->priv->delta_x_len = 0;
    self->priv->delta_z = NULL;
    self->priv->delta_z_len = 0;
    self->priv->detector_diameter = 216.0;
    self->priv->image_width = 190.0;
    self->priv->image_center_x = 0.0;
    self->priv->image_center_y = 0.0;

    // Put default values
    self->priv->source_angle = g_malloc(2 * sizeof(gfloat));
    self->priv->source_angle[0] = 240.0;
    self->priv->source_angle[1] = 240.0;
    self->priv->source_angle_len = 2;

    self->priv->source_diameter = g_malloc(2 * sizeof(gfloat));
    self->priv->source_diameter[0] = 365.0;
    self->priv->source_diameter[1] = 370.0;
    self->priv->source_diameter_len = 2;

    self->priv->delta_x = g_malloc(2 * sizeof(gfloat));
    self->priv->delta_x[0] = 815.0;
    self->priv->delta_x[1] = 815.0;
    self->priv->delta_x_len = 2;

    self->priv->delta_z = g_malloc(2 * sizeof(gfloat));
    self->priv->delta_z[0] = 1417.0;
    self->priv->delta_z[1] = 1430.0;
    self->priv->delta_z_len = 2;
}


static gfloat
ellipse_kreis_uwe (gfloat alpha,
                   gfloat DX,
                   gfloat DZ,
                   gfloat source_ring_diam)
{
    gfloat L, R, CA, eps, p1, p2, gam, ae;

    L = sqrt(DX * DX + DZ * DZ);
    R = 0.5 * source_ring_diam;
    CA = cos(alpha);

    eps = (L * L + R * DX * CA) / (L * sqrt(L * L + R * R + 2.0 * R * DX * CA));
    eps = acos(eps);

    p1 = (L * L - R * DX) / (L * sqrt(L * L + R * R - 2.0 * R * DX));
    p2 = (L * L + R * DX) / (L * sqrt(L * L + R * R + 2.0 * R * DX));

    gam = 0.5 * (acos(p1) - acos(p2));
    ae = (eps * CA + gam) / sqrt(eps * eps + 2.0 * eps * gam * CA + gam * gam);

    if (alpha <= G_PI) {
        return acos(ae);
    }

    return 2.0 * G_PI - acos(ae);
}

static gfloat
deg_to_rad(gfloat angle) {
  return angle * G_PI / 180.0;
}

static gfloat
rad_to_range_0_2PI(gfloat angle) {
  angle += (angle < 0) ? 2 * G_PI : 0;
  angle -= (angle > 2 * G_PI) ? 2 * G_PI : 0;
  return angle;
}

static void
compute_angles (// theta
                gfloat *theta,
                gfloat *theta_after_ray1,
                gfloat *theta_after_ray2,
                gfloat *theta_before_ray1,
                gfloat *theta_before_ray2,
                gfloat *theta_goal_ray1,
                gfloat *theta_goal_ray2,
                // gamma
                gfloat *gamma,
                gfloat *gamma_after_ray1,
                gfloat *gamma_after_ray2,
                gfloat *gamma_before_ray1,
                gfloat *gamma_before_ray2,
                gfloat *gamma_goal_ray1,
                gfloat *gamma_goal_ray2,
                // ray1, ray2
                gfloat *ray1,
                gfloat *ray2,
                // parameters
                const guint index,
                const guint n_fan_dets,
                const guint n_fan_proj,
                const gfloat source_angle,
                const gfloat v_src_r,
                const gfloat delta_x,
                const gfloat delta_z,
                const gfloat detector_r,
                const gfloat alpha_circle,
                const gfloat s,
                const gfloat L,
                const gfloat kappa)
{
    // Supportive variables
    gfloat temp_1 = 0;
    gfloat temp_2 = 0;
    gfloat epsilon = 0;
    gfloat dif_best = 0;
    gfloat dif = 0;
    gfloat best_x = 0;
    guint x = 0;

    // ------- Calculate
    // Theta
    temp_1 = asin(((s - L * sin(alpha_circle - kappa)) / v_src_r));
    theta_goal_ray1[index] = alpha_circle - temp_1;
    theta_goal_ray1[index] = rad_to_range_0_2PI(theta_goal_ray1[index]);
    theta_goal_ray1[index] = ellipse_kreis_uwe(theta_goal_ray1[index],
                                               delta_x,
                                               delta_z,
                                               2 * v_src_r);

    theta_goal_ray2[index] = alpha_circle + temp_1 - G_PI;
    theta_goal_ray2[index] += rad_to_range_0_2PI(theta_goal_ray2[index]);
    theta_goal_ray2[index] = ellipse_kreis_uwe(theta_goal_ray2[index],
                                               delta_x,
                                               delta_z,
                                               2 * v_src_r);

    temp_1 = deg_to_rad((360.0 - source_angle) / 2.0);
    temp_2 = deg_to_rad(360.0 - (360.0 - source_angle) / 2.0);
    ray1[index] = 0;
    ray2[index] = 0;

    if (theta_goal_ray1[index] > temp_1 && theta_goal_ray1[index] < temp_2) {
        ray1[index] = 1;
    }

    if (theta_goal_ray2[index] > temp_1 && theta_goal_ray2[index] < temp_2) {
        ray2[index] = 1;
    }

    epsilon = asin((s - L * sin(alpha_circle - kappa)) / detector_r);

    if (ray1[index]) {
        dif_best = G_PI;

        // Gamma
        gamma_goal_ray1[index] = epsilon + alpha_circle - 1.5 * G_PI;
        gamma_goal_ray1[index] = rad_to_range_0_2PI (gamma_goal_ray1[index]);

        // Vektor Teta nach Wert durchsuchen für Fall 1
        for (x = 0; x < n_fan_proj; x++) {
            if (theta_goal_ray1[index] <= theta[x]) {
                dif = theta[x] - theta_goal_ray1[index];
                if (dif < dif_best) {
                  dif_best = dif;
                  best_x = x;
                }
            }
        }

        if (best_x == 0) {
            theta_before_ray1[index] = n_fan_proj - 1;
            theta_after_ray1[index] = best_x;
        } else {
            theta_before_ray1[index] = best_x - 1;
            theta_after_ray1[index] = best_x;
        }

        //Vektor Gamma nach Wert durchsuchen für Fall 1
        for (x = 0; x < n_fan_dets; x++) {
            if (theta_goal_ray1[index] <= gamma[x]) {
                gamma_before_ray1[index] = (x == 0) ? n_fan_dets - 1 : x - 1;
                gamma_after_ray1[index] = x;
                break;
            }
        }

        if (gamma_goal_ray1[index] > gamma[n_fan_dets - 1]) {
            gamma_before_ray1[index] = n_fan_dets - 1;
            gamma_after_ray1[index] = 0;
        }
    }

    if (ray2[index]) {
        dif_best = G_PI;

        // Gamma
        gamma_goal_ray2[index] = -epsilon + alpha_circle - (G_PI / 2.0);
        gamma_goal_ray2[index] = rad_to_range_0_2PI (gamma_goal_ray1[index]);

        // Vektor Teta nach Wert durchsuchen für Fall 1
        for (x = 0; x < n_fan_proj; x++) {
            if (theta_goal_ray2[index] <= theta[x]) {
                dif = theta[x] - theta_goal_ray2[index];
                if (dif < dif_best) {
                  dif_best = dif;
                  best_x = x;
                }
            }
        }

        if (best_x == 0) {
            theta_before_ray2[index] = n_fan_proj - 1;
            theta_after_ray2[index] = best_x;
        } else {
            theta_before_ray2[index] = best_x - 1;
            theta_after_ray2[index] = best_x;
        }

        //Vektor Gamma nach Wert durchsuchen für Fall 1
        for (x = 0; x < n_fan_dets; x++) {
            if (theta_goal_ray2[index] <= gamma[x]) {
                gamma_before_ray1[index] = (x == 0) ? n_fan_dets - 1 : x - 1;
                gamma_after_ray2[index] = x;
                break;
            }
        }

        if (gamma_goal_ray1[index] > gamma[n_fan_dets - 1]) {
            gamma_before_ray1[index] = n_fan_dets - 1;
            gamma_after_ray1[index] = 0;
        }
    }
}


static void
compute_fan2par_transp (UfoTask *task,
                        UfoBuffer *params_buf)
{
    UfoRofexFan2paraParamsTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PARA_PARAMS_TASK_GET_PRIVATE (task);

    // Task properties
    guint n_dets = priv->n_modules * priv->n_det_per_module;
    guint n_proj = priv->n_proj;
    guint n_planes = priv->n_planes;
    guint n_par_dets = priv->n_par_dets;
    guint n_par_proj = priv->n_par_proj;
    gfloat source_offset = priv->source_offset;
    gfloat detector_r = priv->detector_diameter / 2.0;
    gfloat image_width = priv->image_width;
    gfloat image_center_x = priv->image_center_x;
    gfloat image_center_y = priv->image_center_y;

    gfloat *source_angle = priv->source_angle;
    gfloat *source_diameter = priv->source_diameter;
    gfloat *delta_x = priv->delta_x;
    gfloat *delta_z = priv->delta_z;

    // Get global array of parameters
    UfoRequisition params_req;
    ufo_buffer_get_requisition(params_buf, &params_req);
    guint param_offset = params_req.dims[0];


    gfloat *h_params = ufo_buffer_get_host_array(params_buf, NULL);

    // Parameters
    gfloat *h_theta = h_params + 0;
    gfloat *h_gamma = h_theta + param_offset;
    gfloat *h_s = h_gamma + param_offset;
    gfloat *h_alpha_circle = h_s + param_offset;

    gfloat *h_theta_after_ray1 = h_alpha_circle + param_offset;
    gfloat *h_theta_after_ray2 = h_theta_after_ray1 + param_offset;
    gfloat *h_theta_before_ray1 = h_theta_after_ray2 + param_offset;
    gfloat *h_theta_before_ray2 = h_theta_before_ray1 + param_offset;
    gfloat *h_theta_goal_ray1 = h_theta_before_ray2 + param_offset;
    gfloat *h_theta_goal_ray2 = h_theta_goal_ray1 + param_offset;
    // gamma
    gfloat *h_gamma_after_ray1 = h_theta_goal_ray2 + param_offset;
    gfloat *h_gamma_after_ray2 = h_gamma_after_ray1 + param_offset;
    gfloat *h_gamma_before_ray1 = h_gamma_after_ray2 + param_offset;
    gfloat *h_gamma_before_ray2 = h_gamma_before_ray1 + param_offset;
    gfloat *h_gamma_goal_ray1 = h_gamma_before_ray2 + param_offset;
    gfloat *h_gamma_goal_ray2 = h_gamma_goal_ray1 + param_offset;
    // ray1, ray2
    gfloat *h_ray1 = h_gamma_goal_ray2 + param_offset;
    gfloat *h_ray2 = h_ray1 + param_offset;

    // Precompute some parameters

    // ---- Calculate
    // theta
    for (guint j = 0; j < n_proj; j++) {
        h_theta[j] = deg_to_rad ( (j * 360.0 / (gfloat)n_proj) - source_offset );
        h_theta[j] = rad_to_range_0_2PI(h_theta[j]);
    }

    // gamma
    h_gamma[0] = 0.0;
    for (guint j = 1; j < n_dets; j++) {
        h_gamma[j] = deg_to_rad ( (j * 360.0 / (gfloat)n_dets) );
    }

    // s
    for (guint j = 0; j < n_par_dets; j++) {
        h_s[j] = (-0.5 * image_width)
               + ((0.5 + j) * image_width / (gfloat)n_par_dets);
    }

    // alpha_circle
    // changed loop condition and first value
    for (gint j = n_par_proj-1; j >= 0; j--) {
        h_alpha_circle[j] = deg_to_rad ( (j * 360.0 / (gfloat)n_dets) );
        h_alpha_circle[j] += G_PI / 2.0;
        h_alpha_circle[j] = rad_to_range_0_2PI(h_alpha_circle[j]);
    }

    // calculate hash table
    guint  index = 0;
    gfloat kappa = 0.0;
    gfloat L = 0.0;
    gfloat tb = 0.0;
    gfloat temp_1 = 0.0;

    gfloat v_s, v_alpha_circle, v_src_angle, v_delta_x, v_delta_z, v_src_r;

    if (image_center_y != 0) {
        L = sqrt(image_center_y * image_center_y + image_center_x * image_center_x);
        tb = (image_center_y < 0) ? 1.0 : tb;
        kappa = atan(image_center_x / image_center_x) + tb * G_PI;
    } else if (image_center_x != 0) {
        L = sqrt(image_center_y * image_center_y + image_center_x * image_center_x);
        kappa = (image_center_x < 0) ? -G_PI / 2.0 : G_PI / 2.0;
    }

    guint plane_ind, par_proj_ind, par_det_ind;
    for (plane_ind = 0; plane_ind < n_planes; plane_ind++)
    {
        v_src_angle = source_angle[plane_ind];
        v_src_r = source_diameter[plane_ind] / 2.0;
        v_delta_x = delta_x[plane_ind];
        v_delta_z = delta_z[plane_ind];

        for (par_proj_ind = 0; par_proj_ind < n_par_proj; par_proj_ind++)
        {
            v_alpha_circle = h_alpha_circle[par_proj_ind];

            for (par_det_ind = 0; par_det_ind < n_par_dets; par_det_ind++)
            {
                index = par_det_ind
                      + par_proj_ind * n_par_dets
                      + plane_ind * (n_par_proj * n_par_dets);

                v_s = h_s[par_det_ind];

                temp_1 = (v_s - L * sin(v_alpha_circle - kappa)) / detector_r;

                // is asin possible?
                if (temp_1 <= 1 && temp_1 >= -1) {
                    compute_angles(// theta,
                                  h_theta,
                                  h_theta_after_ray1,
                                  h_theta_after_ray2,
                                  h_theta_before_ray1,
                                  h_theta_before_ray2,
                                  h_theta_goal_ray1,
                                  h_theta_goal_ray2,
                                  // gamma
                                  h_gamma,
                                  h_gamma_after_ray1,
                                  h_gamma_after_ray2,
                                  h_gamma_before_ray1,
                                  h_gamma_before_ray2,
                                  h_gamma_goal_ray1,
                                  h_gamma_goal_ray2,
                                  // ray1, ray2
                                  h_ray1,
                                  h_ray2,
                                  // parameters
                                  index,
                                  n_dets,
                                  n_proj,
                                  v_src_angle,
                                  v_src_r,
                                  v_delta_x,
                                  v_delta_z,
                                  detector_r,
                                  v_alpha_circle,
                                  v_s,
                                  L,
                                  kappa);
                }
            }
        }
    }
}
