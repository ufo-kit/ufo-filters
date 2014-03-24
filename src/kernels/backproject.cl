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
backproject_nearest (global float *sinogram,
                     global float *slice,
                     constant float *sin_lut,
                     constant float *cos_lut,
                     const unsigned int offset,
                     const unsigned n_projections,
                     const float axis_pos,
                     const int overwrite)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int width = get_global_size(0);
    const float bx = idx - axis_pos;
    const float by = idy - axis_pos;
    float sum = 0.0;

    for(int proj = offset; proj < n_projections; proj++) {
        float h = axis_pos + bx * cos_lut[proj] - by * sin_lut[proj];
        sum += sinogram[(int)(proj * width + h)];
    }

    if (overwrite) {
        slice[idy * width + idx] = sum * 4.0 * M_PI;
    }
    else {
        slice[idy * width + idx] = slice[idy * width + idx] + sum * 4.0 * M_PI;
    }
}

__kernel void
backproject_tex (read_only image2d_t sinogram,
                 global float *slice,
                 constant float *sin_lut,
                 constant float *cos_lut,
                 const unsigned int offset,
                 const unsigned int n_projections,
                 const float axis_pos,
                 const int overwrite)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int width = get_global_size(0);
    const float bx = idx - axis_pos;
    const float by = idy - axis_pos;
    float sum = 0.0f;

    for(int proj = offset; proj < n_projections; proj++) {
        /* mad() instructions have a performance impact of about 1% on GTX 580 */
        /* float h = mad (by, sin_lut[proj], mad(bx, cos_lut[proj], axis_pos)); */

        float h = by * sin_lut[proj] + bx * cos_lut[proj] + axis_pos;
        float val = read_imagef (sinogram, volumeSampler, (float2)(h, proj)).x;
        sum += (isnan (val) ? 0.0 : val);
    }

    if (overwrite) {
        slice[idy * width + idx] = sum * 4.0 * M_PI;
    }
    else {
        slice[idy * width + idx] = slice[idy * width + idx] + sum * 4.0 * M_PI;
    }
}

