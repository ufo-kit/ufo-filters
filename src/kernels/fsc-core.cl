/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
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

 /* Accumulate one partial Fourier shell histogram per work-group. Six local
 * uint arrays store five float bit patterns and one integer count.
 */
inline void
atomic_add_float_local (volatile __local uint *address, float value)
{
    uint previous = *address;
    uint expected;

    do {
        expected = previous;
        previous = atomic_cmpxchg (address, expected,
                                   as_uint (as_float (expected) + value));
    } while (previous != expected);
}

inline void
atomic_min_float_local (volatile __local uint *address, float value)
{
    const uint candidate = as_uint (value);
    uint previous = *address;
    uint expected;

    do {
        expected = previous;

        if (candidate >= expected)
            return;

        previous = atomic_cmpxchg (address, expected, candidate);
    } while (previous != expected);
}

inline void
atomic_max_float_local (volatile __local uint *address, float value)
{
    const uint candidate = as_uint (value);
    uint previous = *address;
    uint expected;

    do {
        expected = previous;

        if (candidate <= expected)
            return;

        previous = atomic_cmpxchg (address, expected, candidate);
    } while (previous != expected);
}

kernel void
fsc_accumulate_partials (global const float2 *first,
                         global const float2 *second,
                         global float8 *partials,
                         local uint *shells,
                         uint nx,
                         uint ny,
                         uint nz,
                         float delta_kx,
                         float delta_ky,
                         float delta_kz,
                         float shell_width,
                         uint num_bins,
                         ulong num_voxels)
{
    const size_t local_id = get_local_id (0);
    const size_t local_size = get_local_size (0);
    const size_t group_id = get_group_id (0);
    const size_t global_id = get_global_id (0);
    const size_t global_size = get_global_size (0);

    for (size_t item = local_id; item < 6 * num_bins; item += local_size) {
        const bool is_minimum = item >= 4 * num_bins && item < 5 * num_bins;
        shells[item] = is_minimum ? as_uint (INFINITY) : 0;
    }

    barrier (CLK_LOCAL_MEM_FENCE);

    for (size_t index = global_id; index < num_voxels; index += global_size) {
        const uint x = (uint) (index % nx);
        const size_t yz = index / nx;
        const uint y = (uint) (yz % ny);
        const uint z = (uint) (yz / ny);
        const int qx = x <= nx / 2 ? (int) x : (int) x - (int) nx;
        const int qy = y <= ny / 2 ? (int) y : (int) y - (int) ny;
        const int qz = z <= nz / 2 ? (int) z : (int) z - (int) nz;
        const float kx = (float) qx * delta_kx;
        const float ky = (float) qy * delta_ky;
        const float kz = (float) qz * delta_kz;
        const float radius = sqrt (kx * kx + ky * ky + kz * kz);
        const float shell = floor (radius / shell_width + 0.5f);

        if (shell < (float) num_bins) {
            const uint bin = convert_uint (shell);
            const float2 a = first[index];
            const float2 b = second[index];
            const float cross = a.x * b.x + a.y * b.y;
            const float power_a = dot (a, a);
            const float power_b = dot (b, b);
            const float magnitude_a = sqrt (power_a);
            const float magnitude_b = sqrt (power_b);
            const float minimum = fmin (magnitude_a, magnitude_b);
            const float maximum = fmax (magnitude_a, magnitude_b);

            atomic_add_float_local (&shells[bin], cross);
            atomic_add_float_local (&shells[num_bins + bin], power_a);
            atomic_add_float_local (&shells[2 * num_bins + bin], power_b);
            atomic_inc ((volatile __local uint *) &shells[3 * num_bins + bin]);
            atomic_min_float_local (&shells[4 * num_bins + bin], minimum);
            atomic_max_float_local (&shells[5 * num_bins + bin], maximum);
        }
    }

    barrier (CLK_LOCAL_MEM_FENCE);

    for (size_t bin = local_id; bin < num_bins; bin += local_size) {
        partials[group_id * num_bins + bin] =
            (float8) (as_float (shells[bin]),
                      as_float (shells[num_bins + bin]),
                      as_float (shells[2 * num_bins + bin]),
                      convert_float (shells[3 * num_bins + bin]),
                      as_float (shells[4 * num_bins + bin]),
                      as_float (shells[5 * num_bins + bin]),
                      0.0f,
                      0.0f);
    }
}

/*
 * Output rows are cross sum, first power, second power, sample count,
 * physical shell centre, minimum magnitude and maximum magnitude. Every
 * output element is overwritten.
 */
kernel void
fsc_reduce_partials (global const float8 *partials,
                     global float *output,
                     uint num_groups,
                     uint num_bins,
                     float shell_width)
{
    const size_t bin = get_global_id (0);

    if (bin >= num_bins)
        return;

    float4 total = (float4) (0.0f);
    float minimum = INFINITY;
    float maximum = 0.0f;

    for (uint group = 0; group < num_groups; group++) {
        const float8 partial = partials[group * num_bins + bin];
        total += partial.lo;
        minimum = fmin (minimum, partial.s4);
        maximum = fmax (maximum, partial.s5);
    }

    output[bin] = total.x;
    output[num_bins + bin] = total.y;
    output[2 * num_bins + bin] = total.z;
    output[3 * num_bins + bin] = total.w;
    output[4 * num_bins + bin] = (float) bin * shell_width;
    output[5 * num_bins + bin] = total.w > 0.0f ? minimum : as_float (0x7fc00000u);
    output[6 * num_bins + bin] = total.w > 0.0f ? maximum : as_float (0x7fc00000u);
}
