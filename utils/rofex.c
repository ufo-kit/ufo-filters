#include <stdio.h>
#include <math.h>
#include "rofex.h"


void
make_reordering_schema(guint n_modules,
                       guint n_det_per_module,
                       guint n_fan_proj,
                       guint n_planes,
                       guint n_frames,
                       const gchar *filepath)
{
    gsize n_items = (n_modules * n_det_per_module) * n_fan_proj;
    gsize n_bytes = n_items * sizeof(guint);
    guint *schema = (guint *) g_malloc (n_bytes);

    // ---------- Make the reordering schema
    guint i = 0;
    for (guint proj = 0; proj < n_fan_proj; proj++) {
      for (guint module = 0; module < n_modules; module++) {
        for (guint det = 0; det < n_det_per_module; det++) {
          schema[i++] = det + proj * n_det_per_module
                      + module * (n_det_per_module * n_fan_proj);
        }
      }
    }

    // ---------- Write the reordering schema to the file
    FILE * pFile;
    pFile = fopen (filepath, "wb");
    fwrite (schema, sizeof(guint), n_items, pFile);
    fclose (pFile);

    g_free (schema);
}


gfloat
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


gfloat
deg_to_rad(gfloat angle) {
  return angle * G_PI / 180.0;
}


gfloat
rad_to_range_0_2PI(gfloat angle) {
  angle += (angle < 0) ? 2 * G_PI : 0;
  angle -= (angle > 2 * G_PI) ? 2 * G_PI : 0;
  return angle;
}

void
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
    theta_goal_ray2[index] = rad_to_range_0_2PI(theta_goal_ray2[index]);
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
            if (gamma_goal_ray1[index] <= gamma[x]) {
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
        gamma_goal_ray2[index] = rad_to_range_0_2PI (gamma_goal_ray2[index]);

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
            if (gamma_goal_ray2[index] <= gamma[x]) {
                gamma_before_ray2[index] = (x == 0) ? n_fan_dets - 1 : x - 1;
                gamma_after_ray2[index] = x;
                break;
            }
        }

        if (gamma_goal_ray2[index] > gamma[n_fan_dets - 1]) {
            gamma_before_ray2[index] = n_fan_dets - 1;
            gamma_after_ray2[index] = 0;
        }
    }
}


void
make_fan2par_params (guint n_modules,
                     guint n_det_per_module,
                     guint n_fan_proj,
                     guint n_planes,
                     guint n_par_proj,
                     guint n_par_dets,
                     gfloat source_offset,
                     const gfloat *source_angle,
                     const gfloat *source_diameter,
                     const gfloat *delta_x,
                     const gfloat *delta_z,
                     gfloat detector_diameter,
                     gfloat image_width,
                     gfloat image_center_x,
                     gfloat image_center_y,
                     const gchar *filepath)
{
    /*
      There are 18 parameters:
        14 params, each of (n_par_dets * n_par_proj * n_planes) values.
        1 param, of n_par_dets values
        1 param, of n_par_proj values
        2 params, of n_fan_proj values

      Params are generated for a twice larger number of parallel projections.
    */
    n_par_proj = 2 * n_par_proj;

    guint n_fan_dets = n_modules * n_det_per_module;
    gfloat detector_r = detector_diameter / 2.0;

    guint param_size = n_par_dets * n_par_proj * n_planes;
    param_size = (param_size < n_fan_proj) ? n_fan_proj : param_size;

    guint n_entries = 18 * param_size;
    gsize n_bytes = n_entries * sizeof(gfloat);
    gfloat *params = (gfloat *) g_malloc (n_bytes);
    //
    // For every parameter get associated memory region.
    guint param_offset = param_size;

    gfloat *theta = params + 0;
    gfloat *gamma = theta + param_offset;
    gfloat *s = gamma + param_offset;
    gfloat *alpha_circle = s + param_offset;

    gfloat *theta_after_ray1 = alpha_circle + param_offset;
    gfloat *theta_after_ray2 = theta_after_ray1 + param_offset;
    gfloat *theta_before_ray1 = theta_after_ray2 + param_offset;
    gfloat *theta_before_ray2 = theta_before_ray1 + param_offset;
    gfloat *theta_goal_ray1 = theta_before_ray2 + param_offset;
    gfloat *theta_goal_ray2 = theta_goal_ray1 + param_offset;
    // gamma
    gfloat *gamma_after_ray1 = theta_goal_ray2 + param_offset;
    gfloat *gamma_after_ray2 = gamma_after_ray1 + param_offset;
    gfloat *gamma_before_ray1 = gamma_after_ray2 + param_offset;
    gfloat *gamma_before_ray2 = gamma_before_ray1 + param_offset;
    gfloat *gamma_goal_ray1 = gamma_before_ray2 + param_offset;
    gfloat *gamma_goal_ray2 = gamma_goal_ray1 + param_offset;
    // ray1, ray2
    gfloat *ray1 = gamma_goal_ray2 + param_offset;
    gfloat *ray2 = ray1 + param_offset;

    //
    // Precompute some parameters
    for (guint j = 0; j < n_fan_proj; j++) {
        theta[j] = deg_to_rad ((j * 360.0 / (gfloat)n_fan_proj) - source_offset);
        theta[j] = rad_to_range_0_2PI (theta[j]);
    }

    gamma[0] = 0.0;
    for (guint j = 1; j < n_fan_dets; j++) {
        gamma[j] = deg_to_rad (j * 360.0 / (gfloat)n_fan_dets);
    }

    for (guint j = 0; j < n_par_dets; j++) {
        s[j] = (-0.5 * image_width)
               + ((0.5 + j) * image_width / (gfloat)n_par_dets);
    }

    for (gint j = n_par_proj - 1; j >= 0; j--) {
        alpha_circle[j] = deg_to_rad (j * 360.0 / (gfloat)n_par_proj);
        alpha_circle[j] += G_PI / 2.0;
        alpha_circle[j] = rad_to_range_0_2PI (alpha_circle[j]);
    }

    // calculate hash table
    guint  index = 0;
    gfloat kappa = 0.0;
    gfloat L = 0.0;
    gfloat tb = 0.0;
    gfloat temp_1 = 0.0;

    gfloat v_s, v_alpha_circle, v_src_angle, v_delta_x, v_delta_z, v_src_r;

    if (image_center_y != 0)
    {
        L = sqrt (image_center_y * image_center_y + image_center_x * image_center_x);
        tb = (image_center_y < 0) ? 1.0 : tb;
        kappa = atan (image_center_x / image_center_x) + tb * G_PI;
    }
    else if (image_center_x != 0)
    {
        L = sqrt (image_center_y * image_center_y + image_center_x * image_center_x);
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
            v_alpha_circle = alpha_circle[par_proj_ind];

            for (par_det_ind = 0; par_det_ind < n_par_dets; par_det_ind++)
            {
                index = par_det_ind
                      + par_proj_ind * n_par_dets
                      + plane_ind * (n_par_proj * n_par_dets);

                v_s = s[par_det_ind];

                temp_1 = (v_s - L * sin (v_alpha_circle - kappa)) / detector_r;

                // is asin possible?
                if (temp_1 <= 1 && temp_1 >= -1) {
                    compute_angles(// theta,
                                  theta,
                                  theta_after_ray1,
                                  theta_after_ray2,
                                  theta_before_ray1,
                                  theta_before_ray2,
                                  theta_goal_ray1,
                                  theta_goal_ray2,
                                  // gamma
                                  gamma,
                                  gamma_after_ray1,
                                  gamma_after_ray2,
                                  gamma_before_ray1,
                                  gamma_before_ray2,
                                  gamma_goal_ray1,
                                  gamma_goal_ray2,
                                  // ray1, ray2
                                  ray1,
                                  ray2,
                                  // parameters
                                  index,
                                  n_fan_dets,
                                  n_fan_proj,
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

    // ---------- Write the reordering schema to the file
    FILE * pFile;
    pFile = fopen (filepath, "wb");
    fwrite (params, sizeof(gfloat), n_entries, pFile);
    fclose (pFile);

    g_free (params);
}
