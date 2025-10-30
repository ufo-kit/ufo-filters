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

constant sampler_t volumeSampler = CLK_NORMALIZED_COORDS_FALSE |
                                   CLK_ADDRESS_CLAMP |
                                   CLK_FILTER_LINEAR;

kernel void
backproject_nearest (global float *sinogram,
                     global float *slice,
                     constant float *sin_lut,
                     constant float *cos_lut,
                     const unsigned int x_offset,
                     const unsigned int y_offset,
                     const unsigned int angle_offset,
                     const unsigned n_projections,
                     const float axis_pos)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int width = get_global_size(0);
    const float bx = idx - axis_pos + x_offset + 0.5f;
    const float by = idy - axis_pos + y_offset + 0.5f;
    float sum = 0.0f;

    for(int proj = 0; proj < n_projections; proj++) {
        float h = axis_pos + bx * cos_lut[angle_offset + proj] + by * sin_lut[angle_offset + proj];
        sum += sinogram[(int)(proj * width + h)];
    }

    slice[idy * width + idx] = sum * M_PI_F / n_projections;
}

kernel void
backproject_tex (read_only image2d_t sinogram,
                 global float *slice,
                 constant float *sin_lut,
                 constant float *cos_lut,
                 const unsigned int x_offset,
                 const unsigned int y_offset,
                 const unsigned int angle_offset,
                 const unsigned int n_projections,
                 const float axis_pos)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const float bx = idx - axis_pos + x_offset + 0.5f;
    const float by = idy - axis_pos + y_offset + 0.5f;
    float sum = 0.0f;

#ifdef DEVICE_TESLA_K20XM
#pragma unroll 4
#endif
#ifdef DEVICE_TESLA_P100_PCIE_16GB
#pragma unroll 2
#endif
#ifdef DEVICE_GEFORCE_GTX_TITAN_BLACK
#pragma unroll 8
#endif
#ifdef DEVICE_GEFORCE_GTX_TITAN
#pragma unroll 14
#endif
#ifdef DEVICE_GEFORCE_GTX_1080_TI
#pragma unroll 10
#endif
#ifdef DEVICE_QUADRO_M6000
#pragma unroll 2
#endif
#ifdef DEVICE_GFX1010
#pragma unroll 4
#endif
    for(int proj = 0; proj < n_projections; proj++) {
        float h = by * sin_lut[angle_offset + proj] + bx * cos_lut[angle_offset + proj] + axis_pos;
        sum += read_imagef (sinogram, volumeSampler, (float2)(h, proj + 0.5f)).x;
    }

    slice[idy * get_global_size(0) + idx] = sum * M_PI_F / n_projections;
}

inline float get_sino_point(global float *sino, int x, int y, int width, int height)
{
    x = clamp (x, 0, width - 1);
    y = clamp (y, 0, height - 1);

    return sino[y * width + x];
}

kernel void
backproject_cubic (global float *sinogram,
                   global float *slice,
                   constant float *sin_lut,
                   constant float *cos_lut,
                   const unsigned int x_offset,
                   const unsigned int y_offset,
                   const unsigned int angle_offset,
                   const unsigned n_projections,
                   const float axis_pos)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int width = get_global_size(0);
    const int height = get_global_size(1);
    const float bx = idx - axis_pos + x_offset + 0.5f;
    const float by = idy - axis_pos + y_offset + 0.5f;
    float sum = 0.0f;
    float4 A_0 = (float4)( 2.0f, -2.0f,  1.0f,  1.0f);
    float4 A_1 = (float4)(-3.0f,  3.0f, -2.0f, -1.0f);
    float4 A_2 = (float4)( 0.0f,  0.0f,  1.0f,  0.0f);
    float4 A_3 = (float4)( 1.0f,  0.0f,  0.0f,  0.0f);
    float xif, xf, tmp;
    int xi;
    float4 data;

    for(int proj = 0; proj < n_projections; proj++) {
        float h = axis_pos + bx * cos_lut[angle_offset + proj] + by * sin_lut[angle_offset + proj] - 0.5f;
        xf = modf (h, &xif);
        xi = (int) xif;
        tmp    = get_sino_point (sinogram, xi - 1, proj, width, height);
        data.x = get_sino_point (sinogram,     xi, proj, width, height);
        data.y = get_sino_point (sinogram, xi + 1, proj, width, height);
        data.z = get_sino_point (sinogram, xi + 2, proj, width, height);
        data.w = data.z - data.x;
        data.z = data.y - tmp;
        sum += dot (A_0, data) * xf * xf * xf + dot (A_1, data) * xf * xf + dot (A_2, data) * xf + dot(A_3, data);
    }

    slice[idy * width + idx] = sum * M_PI_F / n_projections;
}
