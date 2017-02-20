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
 compute_attenuation (global float *sino_in,
                      global float *sino_out,
                      global float *avg_ref,
                      global float *avg_dark,
                      const float temp,
                      const unsigned int n_dets,
                      const unsigned int n_proj,
                      const unsigned int planeInd)
{
    const int x = get_global_id(0);
    const int y = get_global_id(1);

    if ( x >= n_dets || y >= n_proj)
        return;

    int sinoInd = x + n_dets * y;
    float numerator = (float) (sino_in[sinoInd])
          - avg_dark[x + planeInd * n_dets];

    float denominator = avg_ref[planeInd * n_dets * n_proj + sinoInd]
          - avg_dark[planeInd * n_dets + x];

    if (numerator < temp)
      numerator = temp;
    if (denominator < temp)
      denominator = temp;

    sino_out[sinoInd] = -log(numerator / denominator);
}

kernel
void mask_sino (global float *sino_in,
                global float *mask,
                global float *sino_out,
                const unsigned int n_dets,
                const unsigned int n_proj)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if ( x >= n_dets || y >= n_proj)
      return;

  int sinoInd = x + n_dets * y;
  sino_out[sinoInd] = sino_in[sinoInd] * mask[sinoInd];
}

kernel
void fan2par_set (global float *sino_out,
                  const unsigned int n_par_dets,
                  const unsigned int n_par_proj)
{
    const int i = get_global_id(0);
    const int j = get_global_id(1);

    // TODO: Fixed logic sign. Was &&
    if (i >= n_par_dets || j >= n_par_proj) {
        return;
    }
    sino_out[j * n_par_dets + i] = 0;
}

#pragma OPENCL EXTENSION cl_khr_global_int32_extended_atomics: enable
#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics: enable
#pragma OPENCL EXTENSION cl_khr_int64_base_atomics: enable
#pragma OPENCL EXTENSION cl_khr_int64_extended_atomics: enable

kernel
void fan2par_interp (
                    global const float *sin_fan_data,
                    global float *sin_par_data,
                    global const float *params,
                    const unsigned int param_offset,
                    const unsigned int plane_index,
                    const unsigned int n_fan_dets,
                    const unsigned int n_fan_proj,
                    const unsigned int n_par_dets,
                    const unsigned int n_par_proj,
                    const float detector_r)
{
    const int par_det_ind = get_global_id(0);
    const int par_proj_ind = get_global_id(1);

    if (par_det_ind >= n_par_dets || par_proj_ind >= n_par_proj) {
        return;
    }

    // ---- Pointers to params memory
    // Parameters
    global const float *theta = params + 0;
    global const float *gamma = theta + param_offset;
    global const float *s = gamma + param_offset;
    global const float *alpha_circle = s + param_offset;

    global const float *theta_after_ray1 = alpha_circle + param_offset;
    global const float *theta_after_ray2 = theta_after_ray1 + param_offset;
    global const float *theta_before_ray1 = theta_after_ray2 + param_offset;
    global const float *theta_before_ray2 = theta_before_ray1 + param_offset;
    global const float *theta_goal_ray1 = theta_before_ray2 + param_offset;
    global const float *theta_goal_ray2 = theta_goal_ray1 + param_offset;
    // gamma
    global const float *gamma_after_ray1 = theta_goal_ray2 + param_offset;
    global const float *gamma_after_ray2 = gamma_after_ray1 + param_offset;
    global const float *gamma_before_ray1 = gamma_after_ray2 + param_offset;
    global const float *gamma_before_ray2 = gamma_before_ray1 + param_offset;
    global const float *gamma_goal_ray1 = gamma_before_ray2 + param_offset;
    global const float *gamma_goal_ray2 = gamma_goal_ray1 + param_offset;
    // ray1, ray2
    global const float *ray1 = gamma_goal_ray2 + param_offset;
    global const float *ray2 = ray1 + param_offset;

    // -----
    // Supportive variables for interpolation
    float W_ziel1 = 0;
    float W_ziel2 = 0;
    float W_ziel_end = 0;
    float V1 = 0, V2 = 0;
    float W1 = 0, W2 = 0, W3 = 0, W4 = 0;

    unsigned long long index = par_det_ind + par_proj_ind * n_par_dets
                             + plane_index * (n_par_dets * n_par_proj);

    float temp_1 = s[par_det_ind] / detector_r;
    unsigned int idx = 0;
    unsigned int idx2 = 0;

    // if asin possible
    if (temp_1 >= -1 && temp_1 <= 1 ) {
        if (ray1[index]) {
            // Interpolationspunkte nehmen fur Fall 1
            idx = theta_before_ray1[index] * n_fan_dets + gamma_before_ray1[index];
            W1 = sin_fan_data[idx];

            idx = theta_before_ray1[index] * n_fan_dets + gamma_after_ray1[index];
            W2 = sin_fan_data[idx];

            idx = theta_after_ray1[index] * n_fan_dets + gamma_before_ray1[index];
            W3 = sin_fan_data[idx];

            idx = theta_after_ray1[index] * n_fan_dets + gamma_after_ray1[index];
            W4 = sin_fan_data[idx];

            // Interpolation durchfuhren fur Fall 1
            idx = (unsigned int)theta_before_ray1[index];
            idx2 = (unsigned int)theta_after_ray1[index];
            temp_1 = theta_goal_ray1[index] - theta[idx];
            temp_1 /= theta[idx2] - theta[idx];
            V1 = W1 + temp_1 * (W3 - W1);
            V2 = W1 + temp_1 * (W4 - W2);

            idx = (unsigned int)gamma_before_ray1[index];
            idx2 = (unsigned int)gamma_after_ray1[index];
            temp_1 = gamma_goal_ray1[index] - gamma[idx];
            temp_1 /= gamma[idx2] - gamma[idx];
            W_ziel1 = V1 + temp_1 * (V2 - V1);
        }


        if (ray2[index]) {
            // Interpolationspunkte nehmen fur Fall 2
            idx = theta_before_ray2[index] * n_fan_dets + gamma_before_ray2[index];
            W1 = sin_fan_data[idx];

            idx = theta_before_ray2[index] * n_fan_dets + gamma_after_ray2[index];
            W2 = sin_fan_data[idx];

            idx = theta_after_ray2[index] * n_fan_dets + gamma_before_ray2[index];
            W3 = sin_fan_data[idx];

            idx = theta_after_ray2[index] * n_fan_dets + gamma_after_ray2[index];
            W4 = sin_fan_data[idx];

            // Interpolation durchfuhren fur Fall 2
            idx = (unsigned int)theta_before_ray2[index];
            idx2 = (unsigned int)theta_after_ray2[index];
            temp_1 = theta_goal_ray2[index] - theta[idx];
            temp_1 /= theta[idx2] - theta[idx];
            V1 = W1 + temp_1 * (W3 - W1);
            V2 = W1 + temp_1 * (W4 - W2);

            idx = (unsigned int)gamma_before_ray2[index];
            idx2 = (unsigned int)gamma_after_ray2[index];
            temp_1 = gamma_goal_ray2[index] - gamma[idx];
            temp_1 /= gamma[idx2] - gamma[idx];
            W_ziel2 = V1 + temp_1 * (V2 - V1);
        }

        // floor might be required because of float on ray_1, ray_2
        if (ray1[index] + ray2[index] > 0) {
          W_ziel_end = (float)ray1[index] * W_ziel1 /
                       ((float)ray1[index] + (float)ray2[index])
                        +
                       (float)ray2[index]  * W_ziel2 /
                       ((float)ray1[index] + (float)ray2[index]);
        }
    }

    // Conversion from 360 to 180 deg
    const int n_par_dets_2 = n_par_dets / 2;
    int mirror_offset = 0;

    if (par_det_ind < n_par_dets_2) {
        mirror_offset = n_par_dets - par_det_ind - 1;
    } else {
        mirror_offset = n_par_dets_2 - (par_det_ind % n_par_dets_2) - 1;
    }

    if (par_proj_ind < n_par_proj / 2) {
        index = par_det_ind + par_proj_ind * n_par_dets;
        sin_par_data[index] += W_ziel_end * 0.5;
    } else {
        index = par_proj_ind * n_par_dets - (n_par_dets * n_par_proj) / 2 + mirror_offset;
        //index = par_det_ind + par_proj_ind * n_par_dets;
        sin_par_data[index] += W_ziel_end * 0.5;
    }
}
