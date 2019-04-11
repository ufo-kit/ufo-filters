/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
 *
 * The algorithmic part is designed by Julian Moosmann, Institute
 * for Photon Science and Synchrotron Radiation.
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

#define COMMON_SETUP_TIE                                \
    const int width = get_global_size(0);               \
    const int height = get_global_size(1);              \
    int idx = get_global_id(0);                         \
    int idy = get_global_id(1);                         \
    float n_idx = (idx >= width >> 1) ? idx - width : idx; \
    float n_idy = (idy >= height >> 1) ? idy - height : idy; \
    n_idx = n_idx / width; \
    n_idy = n_idy / height; \
    float sin_arg = prefac * (n_idy * n_idy + n_idx * n_idx); \

#define COMMON_SETUP    \
    COMMON_SETUP_TIE;   \
    float sin_value = sin(sin_arg);

kernel void
tie_method(float prefac, float regularize_rate, float binary_filter_rate, float frequency_cutoff, global float *output)
{
    COMMON_SETUP_TIE;
    if (sin_arg >= frequency_cutoff)
        output[idy * width + idx] = 0.0f;
    else
        output[idy * width + idx] = 0.5f / (sin_arg + pow(10, -regularize_rate));
}

kernel void
ctf_method(float prefac, float regularize_rate, float binary_filter_rate, float frequency_cutoff, global float *output)
{
    COMMON_SETUP;
    if (sin_arg >= frequency_cutoff)
        output[idy * width + idx] = 0.0f;
    else
        output[idy * width + idx] = 0.5f * sign(sin_value) / (fabs(sin_value) + pow(10, -regularize_rate));
}

kernel void
qp_method(float prefac, float regularize_rate, float binary_filter_rate, float frequency_cutoff, global float *output)
{
    COMMON_SETUP;

    if (sin_arg > M_PI_2_F && fabs (sin_value) < binary_filter_rate || sin_arg >= frequency_cutoff)
        output[idy * width + idx] = 0.0f;
    else
        output[idy * width + idx] = 0.5f * sign(sin_value) / (fabs(sin_value) + pow(10, -regularize_rate));
}

kernel void
qp2_method(float prefac, float regularize_rate, float binary_filter_rate, float frequency_cutoff, global float *output)
{
    COMMON_SETUP;
    float cacl_filter_value = 0.5f * sign(sin_value) / (fabs(sin_value) + pow(10, -regularize_rate));

    if (sin_arg > M_PI_2_F && fabs(sin_value) < binary_filter_rate || sin_arg >= frequency_cutoff)
        output[idy * width + idx] = sign(cacl_filter_value) / (2 * (binary_filter_rate + pow(10, -regularize_rate)));
    else
        output[idy * width + idx] = cacl_filter_value;
}

kernel void
mult_by_value(global float *input, global float *values, global float *output)
{
    int idx = get_global_id(1) * get_global_size(0) + get_global_id(0);
    /* values[idx >> 1] because the filter is real (its width is *input* width / 2)
     * and *input* is complex with real (idx) and imaginary part (idx + 1)
     * interleaved. Thus, two consecutive *input* values are multiplied by the
     * same filter value. */
    output[idx] = input[idx] * values[idx >> 1];
}
