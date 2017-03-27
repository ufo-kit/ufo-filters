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

kernel void
reorder (global const float *input,
         global float *output,
         global const unsigned int *schema,
         const unsigned int n_fan_dets,
         const unsigned int n_fan_proj,
         const unsigned int n_images)
{
    const unsigned long det = get_global_id(0);
    const unsigned long proj = get_global_id(1);
    const unsigned long img = get_global_id(2);

    if (det >= n_fan_dets || proj >= n_fan_proj || img >= n_images) {
      return;
    }

    unsigned long out_idx, in_idx;
    out_idx = det + proj * n_fan_dets + img * (n_fan_dets * n_fan_proj);
    in_idx = schema[det + proj * n_fan_dets] + img * (n_fan_dets * n_fan_proj);

    output[out_idx] = input[in_idx];
}

kernel void
compute_attenuation (global float *sino_in,
                     global float *sino_out,
                     global float *avg_flats,
                     global float *avg_darks,
                     const unsigned int n_fan_dets,
                     const unsigned int n_fan_proj,
                     const unsigned int n_planes,
                     const unsigned int plane_index)
{
    #define EPS 1E-5
    const unsigned long det = get_global_id(0);
    const unsigned long proj = get_global_id(1);
    const unsigned long img = get_global_id(2);

    if (det >= n_fan_dets || proj >= n_fan_proj)
        return;

    unsigned long idx_sino, idx_data, idx_darks, idx_flats;

    idx_sino = det + n_fan_dets * proj;
    idx_data = idx_sino + img * (n_fan_proj * n_fan_dets);
    idx_darks = det + plane_index * n_fan_dets;
    idx_flats = idx_sino + plane_index * (n_fan_proj * n_fan_dets);

    float numerator = sino_in[idx_data] - avg_darks[idx_darks];
    float denominator = avg_flats[idx_flats] - avg_darks[idx_darks];

    numerator = (numerator < EPS) ? EPS : numerator;
    denominator = (denominator < EPS) ? EPS : denominator;

    sino_out[idx_data] = -log (numerator / denominator);
    #undef EPS
}

kernel
void fan2par_set (global float *sino_out,
                  const unsigned int n_par_dets,
                  const unsigned int n_par_proj)
{
    const long det = get_global_id(0);
    const long proj = get_global_id(1);
    const long img = get_global_id(2);

    // TODO: Fixed logic sign. Was &&
    if (det >= n_par_dets || proj >= n_par_proj) {
        return;
    }

    unsigned long index;
    index = det + proj * n_par_dets + img * (n_par_proj * n_par_dets);
    sino_out[index] = 0;
}

// --- Fan 2 par

float
interp_ray (unsigned long index,
            unsigned int n_fan_dets,
            unsigned long sino_fan_offset,
            global const float *sino_fan,
            global const float *gamma,
            global const float *gamma_before,
            global const float *gamma_after,
            global const float *gamma_goal,
            global const float *theta,
            global const float *theta_before,
            global const float *theta_after,
            global const float *theta_goal)
{
    unsigned long idxA, idxB;
    float factor;
    float W1, W2, W3, W4;
    float V1, V2;

    idxA = gamma_before[index] + theta_before[index] * n_fan_dets;
    W1 = sino_fan[idxA + sino_fan_offset];

    idxA = gamma_after[index] + theta_before[index] * n_fan_dets;
    W2 = sino_fan[idxA + sino_fan_offset];

    idxA = gamma_before[index] + theta_after[index] * n_fan_dets;
    W3 = sino_fan[idxA + sino_fan_offset];

    idxA = gamma_after[index] + theta_after[index] * n_fan_dets;
    W4 = sino_fan[idxA + sino_fan_offset];

    idxA = (unsigned long) theta_before[index];
    idxB = (unsigned long) theta_after[index];

    factor = theta_goal[index] - theta[idxA];
    factor = factor / (theta[idxB] - theta[idxA]);
    V1 = W1 + factor * (W3 - W1);
    V2 = W2 + factor * (W4 - W2);

    idxA = (unsigned long) gamma_before[index];
    idxB = (unsigned long) gamma_after[index];
    factor = gamma_goal[index] - gamma[idxA];
    factor = factor / (gamma[idxB] - gamma[idxA]);

    return V1 + factor * (V2 - V1);
}

float
comp_val (unsigned long index,
          unsigned int n_fan_dets,
          unsigned long sino_fan_offset,
          global const float *sino_fan,
          global const float **ray,
          global const float *gamma,
          global const float **gamma_before,
          global const float **gamma_after,
          global const float **gamma_goal,
          global const float *theta,
          global const float **theta_before,
          global const float **theta_after,
          global const float **theta_goal)
{
    float v, factor, res;
    factor = 1.0 / (float) (ray[0][index] + ray[1][index]);
    res = 0;

    for (int i = 0; i < 2; i++) {
        if (ray[i][index]) {
            v = interp_ray(index, n_fan_dets, sino_fan_offset, sino_fan,
                        gamma, gamma_before[i], gamma_after[i], gamma_goal[i],
                        theta, theta_before[i], theta_after[i], theta_goal[i]);

            res += ray[i][index] * v * factor;
        }
    }

    return res * 0.5;
}

kernel
void fan2par_interp (global const float *sino_fan,
                     global float *sino_par,
                     global const float *params,
                     const unsigned int param_offset,
                     const unsigned int n_fan_dets,
                     const unsigned int n_fan_proj,
                     const unsigned int n_par_dets,
                     const unsigned int n_par_proj,
                     const unsigned int n_planes,
                     const float detector_r,
                     const int p_idx)
{
    unsigned long det = get_global_id(0);
    unsigned long proj = get_global_id(1);
    const unsigned long img = get_global_id(2);

    if (det >= n_par_dets || proj >= n_par_proj)
        return;

    const unsigned long sino_fan_offset = img * n_fan_dets * n_fan_proj;

    unsigned long plane_index = (p_idx < 0) ? img % n_planes : p_idx;

    global const float *theta = params + 0;
    global const float *gamma = theta + param_offset;
    global const float *s = gamma + param_offset;
    global const float *alpha_circle = s + param_offset;

    global const float *theta_after[2] =
      { alpha_circle + param_offset, alpha_circle + 2 * param_offset };

    global const float *theta_before[2] =
      { alpha_circle + 3 * param_offset, alpha_circle + 4 * param_offset };

    global const float *theta_goal[2] =
      { alpha_circle + 5 * param_offset, alpha_circle + 6 * param_offset };

    global const float *gamma_after[2] =
      { alpha_circle + 7 * param_offset, alpha_circle + 8 * param_offset };

    global const float *gamma_before[2] =
      { alpha_circle + 9 * param_offset, alpha_circle + 10 * param_offset };

    global const float *gamma_goal[2] =
      { alpha_circle + 11 * param_offset, alpha_circle + 12 * param_offset };

    global const float *ray[2] =
      { alpha_circle + 13 * param_offset, alpha_circle + 14 * param_offset };

    // Output index
    float factor, res;
    unsigned long n_par_dets2;
    unsigned long out_idx, index;

    res = 0;
    n_par_dets2 = n_par_dets / 2;
    out_idx = det + proj * n_par_dets + img * (n_par_proj * n_par_dets);

    // First part
    factor = s[det] / detector_r;
    index = det + proj * n_par_dets + plane_index * (n_par_proj * n_par_dets);

    if (factor >= - 1 && factor <= 1) {
         res += comp_val(index, n_fan_dets, sino_fan_offset, sino_fan, ray,
                         gamma, gamma_after, gamma_before, gamma_goal,
                         theta, theta_after, theta_before, theta_goal);
    }

    // Second part
    proj = n_par_proj + proj;
    det = (det < n_par_dets2) ? n_par_dets - det - 1 :
                                n_par_dets2 - (det % n_par_dets2) - 1;

    factor = s[det] / detector_r;
    index = det + proj * n_par_dets + plane_index * (n_par_proj * n_par_dets);
    if (factor >= - 1 && factor <= 1) {
        res += comp_val(index, n_fan_dets, sino_fan_offset, sino_fan, ray,
                        gamma, gamma_after, gamma_before, gamma_goal,
                        theta, theta_after, theta_before, theta_goal);
    }

    sino_par[out_idx] = res;
}
