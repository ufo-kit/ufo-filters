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

const sampler_t volumeSampler = CLK_NORMALIZED_COORDS_FALSE |
                                CLK_ADDRESS_CLAMP |
                                CLK_FILTER_LINEAR;

__kernel void
backproject(const int num_proj,
            const int num_bins,
            const float off_x,
            const float off_y,
            __constant float *cos_table,
            __constant float *sin_table,
            __constant float *axis_table,
            __global float *sinogram,
            __global float *slice)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int slice_width = get_global_size(0);

    /* TODO: maybe use float4 for optimal vectorization on Intel CPUs */
    float2 b = (float2) (idx + off_x, idy + off_y);
    float corr;
    float sum = 0.0;

    for(int proj = 0; proj < num_proj; proj++) {
        float2 s = (float2) (cos_table[proj], sin_table[proj]);
        s = s*b;
        corr = axis_table[proj];
        sum += sinogram[(int)(proj*num_bins + corr + s.x - s.y)];
    }

    slice[idy*slice_width + idx] = sum;
}

__kernel void
backproject_tex (__read_only image2d_t sinogram,
                 __global float *slice,
                 __constant float *sin_lut,
                 __constant float *cos_lut,
                 const unsigned int n_projections,
                 const float axis_pos)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int slice_width = get_global_size(0);
    const float bx = idx - axis_pos;
    const float by = idy - axis_pos;
    float sum = 0.0f;

    for(int proj = 0; proj < n_projections; proj++) {
        float h = mad (by, sin_lut[proj], mad(bx, cos_lut[proj], axis_pos));
        float val = read_imagef (sinogram, volumeSampler, (float2)(h, proj)).x;
        sum += (isnan (val) ? 0.0 : val);
    }

    slice[idy * slice_width + idx] = sum * 4.0 * M_PI;
}

