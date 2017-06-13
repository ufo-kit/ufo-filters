/*
 * Copyright (C) 2013-2017 Karlsruhe Institute of Technology
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

#include <ufo/ufo.h>
#include <stdio.h>
#include <math.h>

void
make_detectors_map(guint n_mods_per_ring,
                   guint n_dets_per_module,
                   guint n_fan_proj,
                   guint n_timestamps_per_cycle,
                   guint *ton,
                   guint *toff,
                   guint *dets_order,
                   const gchar *filepath);

void
make_fan2par_params (guint n_fan_proj,
                     guint n_par_proj,
                     guint n_par_dets,
                     gfloat image_width,
                     gfloat image_center_x,
                     gfloat image_center_y,
                     //
                     guint n_rings,
                     guint n_mods_per_ring,
                     guint n_dets_per_module,
                     gfloat source_offset,
                     const gfloat *source_angle,
                     const gfloat *source_diameter,
                     const gfloat *delta_x,
                     const gfloat *delta_z,
                     gfloat det_diameter,
                     const gchar *filepath);

int main (int argc, char *argv[])
{
    const gchar *dets_map_filepath = "/home/ashkarin/Suren/ufo3/test/dets_map.raw";
    const gchar *fan2par_filepath = "/home/ashkarin/Suren/ufo3/test/fan2par.raw";
    //
    //
    guint n_timestamps_per_cycle;
    guint n_par_dets, n_par_proj, n_fan_proj;
    gfloat image_width, image_center_x, image_center_y;

    guint n_rings, n_mods_per_ring, n_dets_per_module;
    gfloat source_offset, detector_diameter;
    guint *ton, *toff, *dets_order;
    gfloat *source_angle, *source_diameter, *delta_x, *delta_z;

    //
    // Set default values
    n_timestamps_per_cycle = 4000;

    n_par_dets = 256;
    n_par_proj = 512;
    n_fan_proj = 1000;
    image_width = 190.0;
    image_center_x = 0.0;
    image_center_y = 0.0;

    n_rings = 2;
    n_mods_per_ring = 18;
    n_dets_per_module = 16;

    source_offset = 23.2;
    detector_diameter = 216;

    ton = (guint *) g_malloc (n_mods_per_ring * sizeof(guint));
    toff = (guint *) g_malloc (n_mods_per_ring * sizeof(guint));
    dets_order = (guint *) g_malloc (n_dets_per_module * sizeof(guint));

    source_angle = (gfloat *) g_malloc (n_rings * sizeof(gfloat));
    source_diameter = (gfloat *) g_malloc (n_rings * sizeof(gfloat));
    delta_x = (gfloat *) g_malloc (n_rings * sizeof(gfloat));
    delta_z = (gfloat *) g_malloc (n_rings * sizeof(gfloat));

    gfloat source_angle_default[2] = {240.0, 240.0};
    gfloat source_diameter_default[2] = { 365.0, 370.0 };
    gfloat delta_x_default[2] = { 815.0, 815.0 };
    gfloat delta_z_default[2] = { 1417.0, 1430.0 };

    guint ton_default[18] = {
        118, 328, 538, 748, 958, 1168, 1378, 1588, 1798,
        118, 328, 538, 748, 958, 1168, 1378, 1588, 1798 };

    guint toff_default[18] = {
        2118, 2328, 2538, 2748, 2958, 3168, 3378, 3588, 3798,
        2118, 2328, 2538, 2748, 2958, 3168, 3378, 3588, 3798 };

    guint dets_order_default[16] = {
        14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };

    for (guint ring = 0; ring < n_rings; ring++) {
        source_angle[ring] = source_angle_default[ring];
        source_diameter[ring] = source_diameter_default[ring];
        delta_x[ring] = delta_x_default[ring];
        delta_z[ring] = delta_z_default[ring];
    }

    for (guint module = 0; module < n_mods_per_ring; module++) {
        ton[module] = ton_default[module];
        toff[module] = toff_default[module];
    }

    for (guint det = 0; det < n_dets_per_module; det++) {
        dets_order[det] = dets_order_default[det];
    }

    //
    // Precompute parameters
    make_detectors_map (n_mods_per_ring,
                        n_dets_per_module,
                        n_fan_proj,
                        n_timestamps_per_cycle,
                        ton,
                        toff,
                        dets_order,
                        dets_map_filepath);

    make_fan2par_params (n_fan_proj,
                         n_par_proj,
                         n_par_dets,
                         image_width,
                         image_center_x,
                         image_center_y,
                         //
                         n_rings,
                         n_mods_per_ring,
                         n_dets_per_module,
                         source_offset,
                         source_angle,
                         source_diameter,
                         delta_x,
                         delta_z,
                         detector_diameter,
                         fan2par_filepath);

    //
    // Free
    g_free (ton);
    g_free (toff);
    g_free (dets_order);

    g_free (source_angle);
    g_free (source_diameter);
    g_free (delta_x);
    g_free (delta_z);

    return 0;
}


void
make_detectors_map(guint n_mods_per_ring,
                   guint n_dets_per_module,
                   guint n_fan_proj,
                   guint n_timestamps_per_cycle,
                   guint *ton,
                   guint *toff,
                   guint *dets_order,
                   const gchar *filepath)
{
    gboolean enabled;
    guint n_modpairs_per_ring, n_fan_dets;
    guint timestamp, det_offset, det_start, det_end, map_idx, det_idx;

    guint n_vals;
    gsize n_bytes;
    guint *dets_map;

    n_modpairs_per_ring = n_mods_per_ring / 2;
    n_fan_dets = n_mods_per_ring * n_dets_per_module;

    n_vals = n_fan_dets * n_fan_proj;
    n_bytes = n_vals * sizeof(guint);
    dets_map = (guint *) g_malloc0 (n_bytes);

    //
    for (guint proj = 0; proj < n_fan_proj; proj++) {
        timestamp = n_timestamps_per_cycle * (proj + 1) / n_fan_proj;

        for (guint modpair = 0; modpair < n_modpairs_per_ring; modpair++) {
            enabled = timestamp >= ton[modpair] && timestamp < toff[modpair];
            det_offset = enabled ? n_fan_dets / 2 : 0;

            det_start = (modpair * n_dets_per_module + det_offset) % n_fan_dets;
            det_end = ((modpair + 1) * n_dets_per_module + det_offset) % n_fan_dets;
            det_end = det_end ? det_end : n_fan_dets;

            for (guint det = det_start; det < det_end; det++) {
                map_idx = det + proj * n_fan_dets;
                det_idx = det % n_dets_per_module;
                dets_map[map_idx] = dets_order[det_idx] + 1;
            }
        }
    }

    //
    // Write the detectors map into the file
    FILE * pFile;
    pFile = fopen (filepath, "wb");
    fwrite (dets_map, sizeof(guint), n_vals, pFile);
    fclose (pFile);

    g_free (dets_map);
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
compute_angles (const guint index,
                const guint n_fan_dets,
                const guint n_fan_proj,
                const gfloat detector_diameter,
                const gfloat L,
                const gfloat kappa,
                const gfloat source_angle,
                const gfloat source_diameter,
                const gfloat delta_x,
                const gfloat delta_z,
                const gfloat alpha_circle,
                const gfloat s,
                // theta
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
                gfloat *ray2)
{
    gfloat source_radius, detector_radius;
    gfloat theta_limit[2];
    gfloat t, diff, diff_min;
    guint  proj_best;

    //
    // Copmputation
    source_radius = source_diameter / 2.0;
    detector_radius = detector_diameter / 2.0;
    t = (s - L * sin(alpha_circle - kappa)) / source_radius;
    t = asin(t);

    theta_goal_ray1[index] = rad_to_range_0_2PI(alpha_circle - t);
    theta_goal_ray1[index] = ellipse_kreis_uwe(theta_goal_ray1[index],
                                               delta_x,
                                               delta_z,
                                               source_diameter);

    theta_goal_ray2[index] = rad_to_range_0_2PI(alpha_circle + t - G_PI);
    theta_goal_ray2[index] = ellipse_kreis_uwe(theta_goal_ray2[index],
                                               delta_x,
                                               delta_z,
                                               source_diameter);

    theta_limit[0] = deg_to_rad((360.0 - source_angle) / 2.0);
    theta_limit[1] = 2 * G_PI - theta_limit[0];

    ray1[index] = 0;
    ray2[index] = 0;
    if (theta_goal_ray1[index] > theta_limit[0] &&
        theta_goal_ray1[index] < theta_limit[1])
    {
        ray1[index] = 1;
    }

    if (theta_goal_ray2[index] > theta_limit[0] &&
        theta_goal_ray2[index] < theta_limit[1])
    {
        ray2[index] = 1;
    }

    t = (s - L * sin(alpha_circle - kappa)) / detector_radius;
    t = asin(t);

    if ((gint)ray1[index]) {
        diff_min = G_PI;

        // Gamma
        gamma_goal_ray1[index] = t + alpha_circle - 1.5 * G_PI; // TODO: Is 1.5 correct?
        gamma_goal_ray1[index] = rad_to_range_0_2PI (gamma_goal_ray1[index]);

        // Find projection with minimal difference
        for (guint proj = 0; proj < n_fan_proj; proj++) {
            if (theta_goal_ray1[index] <= theta[proj]) {
                diff = theta[proj] - theta_goal_ray1[index];
                if (diff < diff_min) {
                  diff_min = diff;
                  proj_best = proj;
                }
            }
        }

        if (proj_best == 0) {
            theta_before_ray1[index] = n_fan_proj - 1;
            theta_after_ray1[index] = proj_best;
        } else {
            theta_before_ray1[index] = proj_best - 1;
            theta_after_ray1[index] = proj_best;
        }

        for (guint det = 0; det < n_fan_dets; det++) {
            if (gamma_goal_ray1[index] <= gamma[det]) {
                gamma_before_ray1[index] = (det == 0) ? n_fan_dets - 1 : det - 1;
                gamma_after_ray1[index] = det;
                break;
            }
        }

        if (gamma_goal_ray1[index] > gamma[n_fan_dets - 1]) {
            gamma_before_ray1[index] = n_fan_dets - 1;
            gamma_after_ray1[index] = 0;
        }
    }

    if ((gint)ray2[index]) {
        diff_min = G_PI;

        // Gamma
        gamma_goal_ray2[index] = -t + alpha_circle - (G_PI / 2.0);
        gamma_goal_ray2[index] = rad_to_range_0_2PI (gamma_goal_ray2[index]);

        for (guint proj = 0; proj < n_fan_proj; proj++) {
            if (theta_goal_ray2[index] <= theta[proj]) {
                diff = theta[proj] - theta_goal_ray2[index];
                if (diff < diff_min) {
                  diff_min = diff;
                  proj_best = proj;
                }
            }
        }

        if (proj_best == 0) {
            theta_before_ray2[index] = n_fan_proj - 1;
            theta_after_ray2[index] = proj_best;
        } else {
            theta_before_ray2[index] = proj_best - 1;
            theta_after_ray2[index] = proj_best;
        }

        //Vektor Gamma nach Wert durchsuchen für Fall 1
        for (guint det = 0; det < n_fan_dets; det++) {
            if (gamma_goal_ray2[index] <= gamma[det]) {
                gamma_before_ray2[index] = (det == 0) ? n_fan_dets - 1 : det - 1;
                gamma_after_ray2[index] = det;
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
make_fan2par_params (guint n_fan_proj,
                     guint n_par_proj,
                     guint n_par_dets,
                     gfloat image_width,
                     gfloat image_center_x,
                     gfloat image_center_y,
                     //
                     guint n_rings,
                     guint n_mods_per_ring,
                     guint n_dets_per_module,
                     gfloat source_offset,
                     const gfloat *source_angle,
                     const gfloat *source_diameter,
                     const gfloat *delta_x,
                     const gfloat *delta_z,
                     gfloat detector_diameter,
                     const gchar *filepath)
{
    /*
      There are 18 parameters:
        14 params, each of (n_par_dets * n_par_proj * n_rings) values.
        1 param, of n_par_dets values
        1 param, of n_par_proj values
        2 params, of n_fan_proj values

      Params are generated for a twice larger number of parallel projections
      than planned. It is comes from the fact that final parallel sinogram
      is build of two parts.
    */
    const guint n_params = 18;
    n_par_proj = 2 * n_par_proj;

    //
    guint index;
    gfloat kappa, L, temp;

    guint n_fan_dets, detector_radius;
    guint param_size;
    gfloat *params;
    gfloat *theta, *gamma, *s, *alpha_circle,
           // Theta
           *theta_after_ray1, *theta_after_ray2,
           *theta_before_ray1, *theta_before_ray2,
           *theta_goal_ray1, *theta_goal_ray2,
           // Gamma
           *gamma_after_ray1, *gamma_after_ray2,
           *gamma_before_ray1, *gamma_before_ray2,
           *gamma_goal_ray1, *gamma_goal_ray2,
           // Bundaries
           *ray1, *ray2;

    // Allocate
    param_size = n_par_dets * n_par_proj * n_rings;
    param_size = (param_size < n_fan_proj) ? n_fan_proj : param_size;

    params = (gfloat *) g_malloc (n_params * param_size * sizeof(gfloat));

    // Initiate pointers to parameters
    gfloat **pparams[18] = {    // n_params
        &theta, &gamma, &s, &alpha_circle,
        &theta_after_ray1, &theta_after_ray2,
        &theta_before_ray1, &theta_before_ray2,
        &theta_goal_ray1, &theta_goal_ray2,
        &gamma_after_ray1, &gamma_after_ray2,
        &gamma_before_ray1, &gamma_before_ray2,
        &gamma_goal_ray1, &gamma_goal_ray2,
        &ray1, &ray2 };

    for (guint i = 0; i < n_params; i++) {
        *pparams[i] = params + i * param_size;
    }

    // Compute parameters
    detector_radius = detector_diameter / 2.0;
    n_fan_dets = n_mods_per_ring * n_dets_per_module;

    for (guint i = 0; i < n_fan_proj; i++) {
        theta[i] = i * 360.0 / (gfloat) n_fan_proj  - source_offset;
        theta[i] = rad_to_range_0_2PI (deg_to_rad (theta[i]));
    }

    gamma[0] = 0.0;
    for (guint i = 1; i < n_fan_dets; i++) {
        gamma[i] = i * 360.0 / (gfloat) n_fan_dets;
        gamma[i] = deg_to_rad (gamma[i]);
    }

    for (guint i = 0; i < n_par_dets; i++) {
        s[i] = (-0.5 * image_width)
             + ( (0.5 + i) * image_width / (gfloat) n_par_dets );
    }

    for (gint i = n_par_proj - 1; i >= 0; i--) {
        alpha_circle[i] = i * 360.0 / (gfloat) n_par_proj;
        alpha_circle[i] = deg_to_rad (alpha_circle[i]) + G_PI / 2.0;
        alpha_circle[i] = rad_to_range_0_2PI (alpha_circle[i]);
    }

    // Comnpute hash table
    kappa = 0;
    L = 0.0;

    if (image_center_y) {
        L = sqrt (image_center_y * image_center_y + image_center_x * image_center_x);
        kappa = atan (image_center_x / image_center_x)
              + (image_center_y < 0 ? G_PI : 0.0);
    }
    else if (image_center_x) {
        L = sqrt (image_center_y * image_center_y + image_center_x * image_center_x);
        kappa = (image_center_x < 0) ? -G_PI / 2.0 : G_PI / 2.0;
    }

    for (guint ring = 0; ring < n_rings; ring++) {
        for (guint proj = 0; proj < n_par_proj; proj++) {
            for (guint det = 0; det < n_par_dets; det++) {
                index = det + proj * n_par_dets + ring * (n_par_dets * n_par_proj);
                temp = (s[det] - L * sin (alpha_circle[proj] - kappa)) / detector_radius;

                if (temp <= 1 && temp >= -1) {
                    compute_angles (index,
                                    n_fan_dets,
                                    n_fan_proj,
                                    detector_diameter,
                                    L,
                                    kappa,
                                    source_angle[ring],
                                    source_diameter[ring],
                                    delta_x[ring],
                                    delta_z[ring],
                                    alpha_circle[proj],
                                    s[det],
                                    // theta
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
                                    ray2);
                }
            }
        }
    }

    // ---------- Write the reordering schema to the file
    FILE * pFile;
    pFile = fopen (filepath, "wb");
    fwrite (params, sizeof(gfloat), n_params * param_size, pFile);
    fclose (pFile);

    g_free (params);
}
