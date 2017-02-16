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
#include "ufo-rofex-fan2par-task.h"


struct _UfoRofexFan2parTaskPrivate {
    cl_context context;
    cl_kernel interp_kernel;
    cl_kernel set_kernel;

    gboolean transp_computed;

    UfoBuffer *theta_buf;
    UfoBuffer *gamma_buf;
    UfoBuffer *s_buf;
    UfoBuffer *alpha_circle_buf;
    UfoBuffer *ray1_buf;
    UfoBuffer *ray2_buf;
    // theta
    UfoBuffer *theta_after_ray1_buf;
    UfoBuffer *theta_after_ray2_buf;
    UfoBuffer *theta_before_ray1_buf;
    UfoBuffer *theta_before_ray2_buf;
    UfoBuffer *theta_goal_ray1_buf;
    UfoBuffer *theta_goal_ray2_buf;
    // gamma
    UfoBuffer *gamma_after_ray1_buf;
    UfoBuffer *gamma_after_ray2_buf;
    UfoBuffer *gamma_before_ray1_buf;
    UfoBuffer *gamma_before_ray2_buf;
    UfoBuffer *gamma_goal_ray1_buf;
    UfoBuffer *gamma_goal_ray2_buf;

    // dev ptrs
    gpointer d_gamma;
    gpointer d_theta;
    gpointer d_alpha_circle;
    gpointer d_s;
    gpointer d_theta_after_ray1;
    gpointer d_theta_after_ray2;
    gpointer d_theta_before_ray1;
    gpointer d_theta_before_ray2;
    gpointer d_gamma_after_ray1;
    gpointer d_gamma_after_ray2;
    gpointer d_gamma_before_ray1;
    gpointer d_gamma_before_ray2;
    gpointer d_theta_goal_ray1;
    gpointer d_theta_goal_ray2;
    gpointer d_gamma_goal_ray1;
    gpointer d_gamma_goal_ray2;
    gpointer d_ray1;
    gpointer d_ray2;

    // Props
    guint n_modules;
    guint n_det_per_module;
    guint n_proj;
    guint n_planes;

    guint n_par_dets;
    guint n_par_proj;

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
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoRofexFan2parTask, ufo_rofex_fan2par_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_ROFEX_FAN2PAR_TASK, UfoRofexFan2parTaskPrivate))

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
ufo_rofex_fan2par_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_ROFEX_FAN2PAR_TASK, NULL));
}

static void
ufo_rofex_fan2par_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    priv->interp_kernel = ufo_resources_get_kernel (resources, "rofex.cl", "fan2par_interp", error);
    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->interp_kernel));

    priv->set_kernel = ufo_resources_get_kernel (resources, "rofex.cl", "fan2par_set", error);
    if (error && *error)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->set_kernel));

    priv->transp_computed = FALSE;

    //
    guint n_dets = priv->n_modules * priv->n_det_per_module;
    guint n_proj = priv->n_proj;
    guint n_planes = priv->n_planes;
    guint n_par_dets = priv->n_par_dets;
    guint n_par_proj = priv->n_par_proj;

    priv->d_gamma = NULL;
    priv->d_theta = NULL;
    priv->d_alpha_circle = NULL;
    priv->d_s = NULL;
    priv->d_theta_after_ray1 = NULL;
    priv->d_theta_after_ray2 = NULL;
    priv->d_theta_before_ray1 = NULL;
    priv->d_theta_before_ray2 = NULL;
    priv->d_gamma_after_ray1 = NULL;
    priv->d_gamma_after_ray2 = NULL;
    priv->d_gamma_before_ray1 = NULL;
    priv->d_gamma_before_ray2 = NULL;
    priv->d_theta_goal_ray1 = NULL;
    priv->d_theta_goal_ray2 = NULL;
    priv->d_gamma_goal_ray1 = NULL;
    priv->d_gamma_goal_ray2 = NULL;
    priv->d_ray1 = NULL;
    priv->d_ray2 = NULL;

    // Allocate
    UfoRequisition req;
    req.n_dims = 1;

    req.dims[0] = n_proj;
    priv->theta_buf = ufo_buffer_new(&req, priv->context);

    req.dims[0] = n_dets;
    priv->gamma_buf = ufo_buffer_new(&req, priv->context);


    req.dims[0] = n_par_dets;
    priv->s_buf = ufo_buffer_new(&req, priv->context);

    req.dims[0] = n_par_proj;
    priv->alpha_circle_buf = ufo_buffer_new(&req, priv->context);

    req.dims[0] = n_par_dets * n_par_proj * n_planes;
    priv->ray1_buf = ufo_buffer_new(&req, priv->context);
    priv->ray2_buf = ufo_buffer_new(&req, priv->context);

    priv->theta_after_ray1_buf = ufo_buffer_new(&req, priv->context);
    priv->theta_after_ray2_buf = ufo_buffer_new(&req, priv->context);
    priv->theta_before_ray1_buf = ufo_buffer_new(&req, priv->context);
    priv->theta_before_ray2_buf = ufo_buffer_new(&req, priv->context);
    priv->theta_goal_ray1_buf = ufo_buffer_new(&req, priv->context);
    priv->theta_goal_ray2_buf = ufo_buffer_new(&req, priv->context);

    priv->gamma_after_ray1_buf = ufo_buffer_new(&req, priv->context);
    priv->gamma_after_ray2_buf = ufo_buffer_new(&req, priv->context);
    priv->gamma_before_ray1_buf = ufo_buffer_new(&req, priv->context);
    priv->gamma_before_ray2_buf = ufo_buffer_new(&req, priv->context);
    priv->gamma_goal_ray1_buf = ufo_buffer_new(&req, priv->context);
    priv->gamma_goal_ray2_buf = ufo_buffer_new(&req, priv->context);
}

static void
ufo_rofex_fan2par_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);

    requisition->n_dims = 2;
    requisition->dims[0] = priv->n_par_dets;
    requisition->dims[1] = priv->n_par_proj;
}

static guint
ufo_rofex_fan2par_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_rofex_fan2par_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 2;
}

static UfoTaskMode
ufo_rofex_fan2par_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
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

static void
compute_angles(UfoTask *task,
              gfloat *r_target,
              guint i,
              guint j,
              guint ind,
              guint k,
              gfloat L,
              gfloat kappa)
{
    gfloat epsilon = 0;

    gfloat dif_best = G_PI;
    gfloat dif = 0;
    gfloat best_x = 0;
    gfloat temp_1 = 0;
    gfloat temp_2 = 0;
    guint x = 0;

    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);

    gfloat *theta = ufo_buffer_get_host_array(priv->theta_buf, NULL);
    gfloat *gamma = ufo_buffer_get_host_array(priv->gamma_buf, NULL);
    gfloat *s = ufo_buffer_get_host_array(priv->s_buf, NULL);
    gfloat *alpha_circle = ufo_buffer_get_host_array(priv->alpha_circle_buf, NULL);
    gfloat *ray1 = ufo_buffer_get_host_array(priv->ray1_buf, NULL);
    gfloat *ray2 = ufo_buffer_get_host_array(priv->ray2_buf, NULL);

    gfloat *theta_after_ray1 = ufo_buffer_get_host_array(priv->theta_after_ray1_buf, NULL);
    gfloat *theta_after_ray2 = ufo_buffer_get_host_array(priv->theta_after_ray2_buf, NULL);
    gfloat *theta_before_ray1 = ufo_buffer_get_host_array(priv->theta_before_ray1_buf, NULL);
    gfloat *theta_before_ray2 = ufo_buffer_get_host_array(priv->theta_before_ray2_buf, NULL);
    gfloat *theta_goal_ray1 = ufo_buffer_get_host_array(priv->theta_goal_ray1_buf, NULL);
    gfloat *theta_goal_ray2 = ufo_buffer_get_host_array(priv->theta_goal_ray2_buf, NULL);

    gfloat *gamma_after_ray1 = ufo_buffer_get_host_array(priv->gamma_after_ray1_buf, NULL);
    gfloat *gamma_after_ray2 = ufo_buffer_get_host_array(priv->gamma_after_ray2_buf, NULL);
    gfloat *gamma_before_ray1 = ufo_buffer_get_host_array(priv->gamma_before_ray1_buf, NULL);
    gfloat *gamma_before_ray2 = ufo_buffer_get_host_array(priv->gamma_before_ray2_buf, NULL);
    gfloat *gamma_goal_ray1 = ufo_buffer_get_host_array(priv->gamma_goal_ray1_buf, NULL);
    gfloat *gamma_goal_ray2 = ufo_buffer_get_host_array(priv->gamma_goal_ray2_buf, NULL);

    gfloat *source_angle = priv->source_angle;
    gfloat *delta_x = priv->delta_x;
    gfloat *delta_z = priv->delta_z;

    gfloat detector_r = priv->detector_diameter / 2.0;
    guint  n_proj = priv->n_proj;
    guint  n_dets = priv->n_modules * priv->n_det_per_module;

    // Set ray1, ray2 to zeros
    //memset(ray1, 10, ufo_buffer_get_size(priv->ray1_buf));
    //memset(ray2, 10, ufo_buffer_get_size(priv->ray2_buf));

    // Berechnungsvorschrift
    // Theta
    temp_1 = asin(((s[i] - L * sin(alpha_circle[j] - kappa)) / r_target[k])); //<-----Veranderung
    theta_goal_ray1[ind] = alpha_circle[j] - temp_1;
    if (theta_goal_ray1[ind] < 0) {
        theta_goal_ray1[ind] = theta_goal_ray1[ind] + 2.0 * G_PI;
    }

    theta_goal_ray1[ind] = ellipse_kreis_uwe(theta_goal_ray1[ind],
                                             delta_x[k],
                                             delta_z[k],
                                             2 * r_target[k]);

    theta_goal_ray2[ind] = alpha_circle[j] + temp_1 - G_PI;
    if (theta_goal_ray2[ind] < 0)
        theta_goal_ray2[ind] = theta_goal_ray2[ind] + 2.0 * G_PI;

    theta_goal_ray2[ind] = ellipse_kreis_uwe(theta_goal_ray2[ind],
                                             delta_x[k],
                                             delta_z[k],
                                             2 * r_target[k]);

    temp_1 = ((360.0 - source_angle[k]) / 2.0) * G_PI / 180.0;
    temp_2 = (360.0 - (360.0 - source_angle[k]) / 2.0) * G_PI / 180.0;

    if (theta_goal_ray1[ind] > temp_1 && theta_goal_ray1[ind] < temp_2)
        ray1[ind] = 1;

    if (theta_goal_ray2[ind] > temp_1 && theta_goal_ray2[ind] < temp_2)
        ray2[ind] = 1;


    //g_print("priv->ray1_buf: %p %p %p\n", priv->ray1_buf, ray1, ufo_buffer_get_host_array(priv->ray1_buf, NULL));
    //g_print("ind: %lu  ray1: %f  ray2: %f\n", ind, ray1[ind], ray2[ind]);

    //g_printf("-- compute_angles -- %d HRAY1, HRAY2 : %p %p | %f %f\n", ind, ray1, ray2, ray1[ind], ray2[ind]);
    epsilon = asin((s[i] - L * sin(alpha_circle[j] - kappa)) / detector_r);  //<-----Veranderung

    if (ray1[ind]) {
        // gamma
        gamma_goal_ray1[ind] = epsilon + alpha_circle[j] - 1.5 * G_PI;
        if (gamma_goal_ray1[ind] < 0) {
            gamma_goal_ray1[ind] = gamma_goal_ray1[ind] + 2.0 * G_PI;
        }

        if (gamma_goal_ray1[ind] > 2* G_PI) {
            gamma_goal_ray1[ind] = gamma_goal_ray1[ind] - 2.0 * G_PI;
        }

        //Vektor Teta nach Wert durchsuchen fur Fall 1
        for (x = 0; x < n_proj; x++) {
            if (theta_goal_ray1[ind] <= theta[x]) {
                dif = theta[x] - theta_goal_ray1[ind];
                if (dif < dif_best) {
                    dif_best = dif;
                    best_x = x;
                }
            }
        }

        if (best_x == 0) {
            theta_before_ray1[ind] = n_proj - 1;
            theta_after_ray1[ind] = best_x;
        } else {
            theta_before_ray1[ind] = best_x - 1;
            theta_after_ray1[ind] = best_x;
        }

        //Vektor Gamma nach Wert durchsuchen fur Fall 1
        for (x = 0; x < n_proj; x++) {
            if (gamma_goal_ray1[ind] <= gamma[x]) {
                if (x == 0) {
                    gamma_before_ray1[ind] = n_dets - 1;
                } else {
                    gamma_before_ray1[ind] = x - 1;
                }
            }
        }
        if (gamma_goal_ray1[ind] > gamma[n_dets - 1]) {
            gamma_before_ray1[ind] = n_dets - 1;
            gamma_after_ray1[ind] = 0;
        }
    }

    if (ray2[ind]) {
        dif_best = G_PI;

        // Gama fur Fall 2
        gamma_goal_ray2[ind] = -epsilon + alpha_circle[j] - G_PI / 2.0;
        if (gamma_goal_ray2[ind] < 0) {
            gamma_goal_ray2[ind] = gamma_goal_ray2[ind] + G_PI * 2.0;
        }

        //Vektor Teta nach Wert durchsuchen fur Fall 2
        for (x = 0; x < n_proj; x++) {
            if (theta_goal_ray2[ind] <= theta[x]) {
                dif = theta[x] - theta_goal_ray2[ind];
                if (dif < dif_best) {
                    dif_best = dif;
                    best_x = x;
                }
            }
        }

        if (best_x == 0) {
            theta_before_ray2[ind] = n_proj - 1;
            theta_after_ray2[ind] = best_x;
        } else {
            theta_before_ray2[ind] = best_x - 1;
            theta_after_ray2[ind] = best_x;
        }

        //Vektor Gamma nach Wert durchsuchen fur Fall 2
        for (x = 0; x < n_proj; x++) {
            if (gamma_goal_ray2[ind] <= gamma[x]) {
                if (x == 0) {
                    gamma_before_ray2[ind] = n_dets - 1;
                } else {
                    gamma_before_ray2[ind] = x - 1;
                }
                gamma_after_ray2[ind] = x;
                break;
            }
        }

        if (gamma_goal_ray2[ind] > gamma[n_dets - 1]) {
            gamma_before_ray2[ind] = n_dets - 1;
            gamma_after_ray2[ind] = 0;
        }
    }
}

static void
compute_fan2par_transp (UfoTask *task)
{
    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);

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

    gfloat *theta = ufo_buffer_get_host_array(priv->theta_buf, NULL);
    gfloat *gamma = ufo_buffer_get_host_array(priv->gamma_buf, NULL);
    gfloat *s = ufo_buffer_get_host_array(priv->s_buf, NULL);
    gfloat *alpha_circle = ufo_buffer_get_host_array(priv->alpha_circle_buf, NULL);

    gfloat *r_target = g_malloc(priv->source_diameter_len * sizeof(gfloat));
    for (guint i = 0; i < priv->source_diameter_len; ++i) {
      r_target[i] = priv->source_diameter[i] / 2.0;
    }

    // === Init values for Hash table
    // ===============================
    // == Theta       = Ortswinkel des Quellpunktes auf Target
    // == Gamma       = Ortswinkel des Detektorpixels
    // == s           = diskreter Abstand der Detektorpixel (Para)
    // == alpha_kreis = Ortswinkel der Parallelstrahlquellen
    const gfloat DEG_360_CONST = 360.0;
    const gfloat DEG_TO_RAD_MOD = 2.0 * G_PI / 360.0;

    // compute: theta angles in radians

    for (guint j = 0; j < n_proj; j++) {
        theta[j] = j * (DEG_360_CONST / (gfloat)n_proj) - source_offset;
        if (theta[j] < 0.0) {
            theta[j] = theta[j] + DEG_360_CONST;
        }

        theta[j] = DEG_TO_RAD_MOD * theta[j];
    }

    // compute: gamma angles in radians
    gamma[0] = 0.0;
    for (guint j = 1; j < n_dets; j++) {
        gamma[j] = j * (DEG_360_CONST / (gfloat)n_dets);
        gamma[j] = DEG_TO_RAD_MOD * gamma[j];
    }

    // compute: s
    for (guint j = 0; j < n_par_dets; j++) {
        s[j] = -0.5 * image_width
               + ((0.5 + j) * image_width / (gfloat)n_par_dets);
    }

    // compute: alpha_circle
    // TODO: changed loop condition and first value
    for (gint j = n_par_proj-1; j >= 0; j--) {
        alpha_circle[j] = j * (DEG_360_CONST / (gfloat)n_par_dets);
        alpha_circle[j] = DEG_TO_RAD_MOD * alpha_circle[j] + G_PI / 2.0;

        if (alpha_circle[j] > 2 * G_PI) {
            alpha_circle[j] = alpha_circle[j] - 2.0 * G_PI;
        }
    }

    // === calculate Hash Table
    // =========================
    unsigned long long ind = 0;
    gfloat kappa = 0.0;
    gfloat L = 0.0;
    gfloat tb = 0.0;

    // Abfrage
    L = sqrt(image_center_y * image_center_y + image_center_x * image_center_x);
    if (image_center_y != 0) {
        if (image_center_y < 0) {
          tb = 1.0;
        }
        kappa = atan(image_center_x / image_center_y) + tb * G_PI;
    } else if (image_center_x != 0) {
        if (image_center_x < 0) {
            kappa = -G_PI / 2.0;
        } else {
            kappa = G_PI / 2.0;
        }
    }

    guint parallel_size = n_par_dets * n_par_proj;
    gfloat temp_1;

    for (guint k = 0; k < n_planes; k++){
        for (guint j = 0; j < n_par_proj; j++) {
            for (guint i = 0; i < n_par_dets; i++) {
                ind = (k * parallel_size) + j * n_par_dets + i;
                temp_1 = (s[i] - L * sin(alpha_circle[j] - kappa)) / detector_r;
                //Prufen, ob asin moglich
                if (temp_1 <= 1 && temp_1 >= -1) {
                    compute_angles(task, r_target, i, j, ind, k, L, kappa);
                    // https://github.com/HZDR-FWDF/RISA/blob/master/RISA/risaLib/src/Fan2Para/Fan2Para.cu#L348
                }
            }
        }
    }
}

static gboolean
ufo_rofex_fan2par_task_process (UfoTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition)
{
    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (task);

    // Get command queue
    UfoGpuNode *node;
    cl_command_queue cmd_queue;
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    //g_printf("ufo_rofex_fan2par_task_process: transp_computed: %d\n", priv->transp_computed);
    if (!priv->transp_computed) {
      compute_fan2par_transp(task);

      priv->d_gamma = ufo_buffer_get_device_array(priv->gamma_buf, cmd_queue);
      priv->d_theta = ufo_buffer_get_device_array(priv->theta_buf, cmd_queue);
      priv->d_alpha_circle = ufo_buffer_get_device_array(priv->alpha_circle_buf, cmd_queue);
      priv->d_s = ufo_buffer_get_device_array(priv->s_buf, cmd_queue);
      priv->d_theta_after_ray1 = ufo_buffer_get_device_array(priv->theta_after_ray1_buf, cmd_queue);
      priv->d_theta_after_ray2 = ufo_buffer_get_device_array(priv->theta_after_ray2_buf, cmd_queue);
      priv->d_theta_before_ray1 = ufo_buffer_get_device_array(priv->theta_before_ray1_buf, cmd_queue);
      priv->d_theta_before_ray2 = ufo_buffer_get_device_array(priv->theta_before_ray2_buf, cmd_queue);
      priv->d_gamma_after_ray1 = ufo_buffer_get_device_array(priv->gamma_after_ray1_buf, cmd_queue);
      priv->d_gamma_after_ray2 = ufo_buffer_get_device_array(priv->gamma_after_ray2_buf, cmd_queue);
      priv->d_gamma_before_ray1 = ufo_buffer_get_device_array(priv->gamma_before_ray1_buf, cmd_queue);
      priv->d_gamma_before_ray2 = ufo_buffer_get_device_array(priv->gamma_before_ray2_buf, cmd_queue);
      priv->d_theta_goal_ray1 = ufo_buffer_get_device_array(priv->theta_goal_ray1_buf, cmd_queue);
      priv->d_theta_goal_ray2 = ufo_buffer_get_device_array(priv->theta_goal_ray2_buf, cmd_queue);
      priv->d_gamma_goal_ray1 = ufo_buffer_get_device_array(priv->gamma_goal_ray1_buf, cmd_queue);
      priv->d_gamma_goal_ray2 = ufo_buffer_get_device_array(priv->gamma_goal_ray2_buf, cmd_queue);
      //priv->d_ray1 = ufo_buffer_get_device_array(priv->ray1_buf, cmd_queue);
      //priv->d_ray2 = ufo_buffer_get_device_array(priv->ray2_buf, cmd_queue);

      guint n_planes2 = priv->n_planes;
      guint n_par_dets2 = priv->n_par_dets;
      guint n_par_proj2 = priv->n_par_proj;
      gfloat *ray1 = ufo_buffer_get_host_array(priv->ray1_buf, NULL);
      gfloat *ray2 = ufo_buffer_get_host_array(priv->ray2_buf, NULL);
      //g_printf("-- process --- HRAY1, HRAY2 : %p %p  | %f %f\n", ray1, ray2, ray1[262122], ray2[262133]);
      //for (guint i = 0; i < n_par_dets2 * n_par_proj2 * n_planes2; i++) {
    //      if (ray1[i] > 0 || ray2[i] > 0)
    //          g_printf("!! %d %f %f \n", i, ray1[i], ray2[i]);
     // }

      priv->d_ray1 = ufo_buffer_get_device_array(priv->ray1_buf, cmd_queue);
      priv->d_ray2 = ufo_buffer_get_device_array(priv->ray2_buf, cmd_queue);

      priv->transp_computed = TRUE;
    }

    guint n_planes2 = priv->n_planes;
    guint n_par_dets2 = priv->n_par_dets;
    guint n_par_proj2 = priv->n_par_proj;
    gfloat *hray1 = ufo_buffer_get_host_array(priv->ray1_buf, NULL);
    gfloat *hray2 = ufo_buffer_get_host_array(priv->ray2_buf, NULL);
    //for (guint i = 0; i < n_par_dets2 * n_par_proj2 * n_planes2; i++) {
    //    if (hray1[i] > 0 || hray2[i] > 0)
    //        g_printf(" -- process -- %d %f %f \n", i, hray1[i], hray2[i]);
    //}

    // Get plane ID for the sinogram
    GValue *gv_plane_index;
    gv_plane_index = ufo_buffer_get_metadata (inputs[0], "plane-index");
    guint plane_index = g_value_get_uint (gv_plane_index);

    // Get device memory
    gpointer d_sino = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    gpointer d_output = ufo_buffer_get_device_array(output, cmd_queue);

    guint n_proj = priv->n_proj;
    guint n_dets = priv->n_modules * priv->n_det_per_module;
    guint n_par_proj = priv->n_par_proj;
    guint n_par_dets = priv->n_par_dets;
    gfloat detector_r = priv->detector_diameter / 2.0;

    // Run
    UfoProfiler *profiler;
    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->set_kernel, 0, sizeof (cl_mem), &d_output));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->set_kernel, 1, sizeof (guint), &n_par_dets));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->set_kernel, 2, sizeof (guint), &n_par_proj));
    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->set_kernel,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 0, sizeof (guint), &plane_index))
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 1, sizeof (cl_mem), &d_sino));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 2, sizeof (cl_mem), &d_output));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 3, sizeof (cl_mem), &priv->d_gamma));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 4, sizeof (cl_mem), &priv->d_theta));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 5, sizeof (cl_mem), &priv->d_alpha_circle));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 6, sizeof (cl_mem), &priv->d_s));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 7, sizeof (cl_mem), &priv->d_theta_after_ray1));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 8, sizeof (cl_mem), &priv->d_theta_after_ray2));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 9, sizeof (cl_mem), &priv->d_theta_before_ray1));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 10, sizeof (cl_mem), &priv->d_theta_before_ray2));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 11, sizeof (cl_mem), &priv->d_gamma_before_ray1));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 12, sizeof (cl_mem), &priv->d_gamma_before_ray2));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 13, sizeof (cl_mem), &priv->d_gamma_after_ray1));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 14, sizeof (cl_mem), &priv->d_gamma_after_ray2));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 15, sizeof (cl_mem), &priv->d_theta_goal_ray1));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 16, sizeof (cl_mem), &priv->d_theta_goal_ray2));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 17, sizeof (cl_mem), &priv->d_gamma_goal_ray1));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 18, sizeof (cl_mem), &priv->d_gamma_goal_ray2));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 19, sizeof (cl_mem), &priv->d_ray1));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 20, sizeof (cl_mem), &priv->d_ray2));

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 21, sizeof (guint), &n_dets));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 22, sizeof (guint), &n_proj));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 23, sizeof (guint), &n_par_dets));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 24, sizeof (guint), &n_par_proj));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->interp_kernel, 25, sizeof (gfloat), &detector_r));

    ufo_profiler_call (profiler,
                       cmd_queue,
                       priv->interp_kernel,
                       requisition->n_dims,
                       requisition->dims,
                       NULL);

    return TRUE;
}


static void
ufo_rofex_fan2par_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFan2parTaskPrivate *priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (object);
    gchar *src_angle_str = NULL;
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
            src_angle_str = g_value_get_string(value);
            angles_str = g_strsplit(src_angle_str, " ", -1);
            g_warning("PROP_SOURCE_ANGLE: %s", src_angle_str);
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

    if (angles_str)
        g_strfreev(angles_str);
}

static void
ufo_rofex_fan2par_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoRofexFan2parTaskPrivate *priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (object);

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
ufo_rofex_fan2par_task_finalize (GObject *object)
{
    UfoRofexFan2parTaskPrivate *priv;
    priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE (object);

    if (priv->theta_buf)
        g_object_unref(priv->theta_buf);

    if (priv->gamma_buf)
        g_object_unref(priv->gamma_buf);

    if (priv->s_buf)
        g_object_unref(priv->s_buf);

    if (priv->alpha_circle_buf)
        g_object_unref(priv->alpha_circle_buf);

    if (priv->ray1_buf)
        g_object_unref(priv->ray1_buf);

    if (priv->ray2_buf)
        g_object_unref(priv->ray2_buf);

    if (priv->theta_after_ray1_buf)
        g_object_unref(priv->theta_after_ray1_buf);

    if (priv->theta_after_ray2_buf)
        g_object_unref(priv->theta_after_ray2_buf);

    if (priv->theta_before_ray1_buf)
        g_object_unref(priv->theta_before_ray1_buf);

    if (priv->theta_before_ray2_buf)
        g_object_unref(priv->theta_before_ray2_buf);

    if (priv->theta_goal_ray1_buf)
        g_object_unref(priv->theta_goal_ray1_buf);

    if (priv->theta_goal_ray2_buf)
        g_object_unref(priv->theta_goal_ray2_buf);

    if (priv->gamma_after_ray1_buf)
        g_object_unref(priv->gamma_after_ray1_buf);

    if (priv->gamma_after_ray2_buf)
        g_object_unref(priv->gamma_after_ray2_buf);

    if (priv->gamma_before_ray1_buf)
        g_object_unref(priv->gamma_before_ray1_buf);

    if (priv->gamma_before_ray2_buf)
        g_object_unref(priv->gamma_before_ray2_buf);

    if (priv->gamma_goal_ray1_buf)
        g_object_unref(priv->gamma_goal_ray1_buf);

    if (priv->gamma_goal_ray2_buf)
        g_object_unref(priv->gamma_goal_ray2_buf);

    if (priv->interp_kernel) {
      UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->interp_kernel));
      priv->interp_kernel = NULL;
    }

    if (priv->set_kernel) {
      UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->set_kernel));
      priv->set_kernel = NULL;
    }

    if (priv->context) {
      UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
      priv->context = NULL;
    }

    G_OBJECT_CLASS (ufo_rofex_fan2par_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_rofex_fan2par_task_setup;
    iface->get_num_inputs = ufo_rofex_fan2par_task_get_num_inputs;
    iface->get_num_dimensions = ufo_rofex_fan2par_task_get_num_dimensions;
    iface->get_mode = ufo_rofex_fan2par_task_get_mode;
    iface->get_requisition = ufo_rofex_fan2par_task_get_requisition;
    iface->process = ufo_rofex_fan2par_task_process;
}

static void
ufo_rofex_fan2par_task_class_init (UfoRofexFan2parTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_rofex_fan2par_task_set_property;
    oclass->get_property = ufo_rofex_fan2par_task_get_property;
    oclass->finalize = ufo_rofex_fan2par_task_finalize;

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

    g_type_class_add_private (oclass, sizeof(UfoRofexFan2parTaskPrivate));
}

static void
ufo_rofex_fan2par_task_init(UfoRofexFan2parTask *self)
{
    self->priv = UFO_ROFEX_FAN2PAR_TASK_GET_PRIVATE(self);
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
