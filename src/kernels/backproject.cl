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

#define PIXELS_PER_THREAD 8

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

inline float get_sino_point (global float *sino, int x, int y, int width, int height)
{
    x = clamp (x, 0, width - 1);
    y = clamp (y, 0, height - 1);

    return sino[y * width + x];
}

/* *backproject_cubic* is an optimized version of this kernel, which is easy to
 * understand and can be used for correctness testing */
kernel void
backproject_cubic_naive (global float *sinogram,
                         global float *slice,
                         constant float *sin_lut,
                         constant float *cos_lut,
                         const unsigned int x_offset,
                         const unsigned int y_offset,
                         const unsigned int angle_offset,
                         const unsigned n_projections,
                         const float axis_pos,
                         const int sino_width)
{
    int idx = get_global_id(0);
    int idy = get_global_id(1);
    int width = get_global_size(0);
    int height = get_global_size(1);
    float bx = idx - axis_pos + x_offset + 0.5f;
    float by = idy - axis_pos + y_offset + 0.5f;
    float sum = 0.0f;
    float4 A_0 = (float4)( 2.0f, -2.0f,  1.0f,  1.0f);
    float4 A_1 = (float4)(-3.0f,  3.0f, -2.0f, -1.0f);
    float4 A_2 = (float4)( 0.0f,  0.0f,  1.0f,  0.0f);
    float4 A_3 = (float4)( 1.0f,  0.0f,  0.0f,  0.0f);
    float xif, xf, tmp;
    int xi;
    float4 data;


    for (int proj = 0; proj < n_projections; proj++) {
        float h = axis_pos + bx * cos_lut[angle_offset + proj] + by * sin_lut[angle_offset + proj] - 0.5f;
        xf = modf (h, &xif);
        xi = (int) xif;
        tmp    = get_sino_point (sinogram, xi - 1, proj, sino_width, height);
        data.x = get_sino_point (sinogram,     xi, proj, sino_width, height);
        data.y = get_sino_point (sinogram, xi + 1, proj, sino_width, height);
        data.z = get_sino_point (sinogram, xi + 2, proj, sino_width, height);
        data.w = data.z - data.x;
        data.z = data.y - tmp;
        sum += dot (A_0, data) * xf * xf * xf + dot (A_1, data) * xf * xf + dot (A_2, data) * xf + dot(A_3, data);
    }

    slice[idy * width + idx] = sum * M_PI_F / n_projections;
}

kernel void
backproject_linear (global float *sinogram,
                    global float *slice,
                    constant float *sin_lut,
                    constant float *cos_lut,
                    const unsigned int x_offset,
                    const unsigned int y_offset,
                    const unsigned int angle_offset,
                    const unsigned n_projections,
                    const float axis_pos,
                    const int width,
                    const int height,
                    const int sino_width,
                    const int between_0_180)
{
    int idx = get_global_id(0);
    int idy;
    int lx = get_local_id (0);
    int gx = get_group_id (0);
    int gy = get_group_id (1);
    int group_width = get_local_size (0);
    float bx = idx - axis_pos + x_offset + 0.5f;
    float by, cos_angle, sin_angle;
    float sum[PIXELS_PER_THREAD];
    float xif, xf, tmp;
    int xi, xi_last, j_stop;
    float sino_local[2];

    if (idx >= width) {
        return;
    }

    j_stop = min (PIXELS_PER_THREAD, height - gy * PIXELS_PER_THREAD);

    for (int j = 0; j < j_stop; j++) {
        sum[j] = 0.0f;
    }

    for (int proj = 0; proj < n_projections; proj++) {
        cos_angle = cos_lut[angle_offset + proj];
        sin_angle = sin_lut[angle_offset + proj];
        tmp = axis_pos + bx * cos_angle - 0.5f;

        for (int j = 0; j < j_stop; j++) {
            idy = gy * PIXELS_PER_THREAD + j;
            by = idy - axis_pos + y_offset + 0.5f;
            xf = modf (tmp + by * sin_angle, &xif);
            xi = (int) xif;

            if (j == 0) {
                /* Initialization: load four neighbors and compute central
                 * difference derivatives. */
                xi_last = xi;
                sino_local[0] = sinogram[proj * sino_width + clamp (xi    , 0, sino_width - 1)];
                sino_local[1] = sinogram[proj * sino_width + clamp (xi + 1, 0, sino_width - 1)];
            }
            if (xi > xi_last) {
                /* Difference to the pixel below can be at most 1 when angle is
                 * 90 degrees, otherwise it is definitely less than one. This is
                 * the case when shift is positive, so we replace the right
                 * pixel. We shift all pixels and load a new one, this is way
                 * faster than ring buffer rotation. */
                sino_local[0] = sino_local[1];
                sino_local[1] = sinogram[proj * sino_width + clamp (xi + 1, 0, sino_width - 1)];
            }
            if (!between_0_180 && xi < xi_last) {
                /* This is for angles between 180 - 360 degrees, where the shift
                 * is negative. We replace the left pixel. */
                sino_local[1] = sino_local[0];
                sino_local[0] = sinogram[proj * sino_width + clamp (xi, 0, sino_width - 1)];
            }

            sum[j] += sino_local[0] * (1.0f - xf) + sino_local[1] * xf;
            xi_last = xi;
        }
    }

    for (int j = 0; j < j_stop; j++) {
        idy = gy * PIXELS_PER_THREAD + j;
        slice[idy * width + idx] = sum[j] * M_PI_F / n_projections;
    }
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
                   const float axis_pos,
                   const int width,
                   const int height,
                   const int sino_width,
                   const int between_0_180)
{
    int idx = get_global_id(0);
    int idy;
    int lx = get_local_id (0);
    int gx = get_group_id (0);
    int gy = get_group_id (1);
    int group_width = get_local_size (0);
    float bx = idx - axis_pos + x_offset + 0.5f;
    float by, cos_angle, sin_angle;
    float sum[PIXELS_PER_THREAD];
    float xif, xf, tmp;
    int xi, xi_last, j_stop;
    float d_1, d_2;
    float sino_local[4];

    if (idx >= width) {
        return;
    }

    j_stop = min (PIXELS_PER_THREAD, height - gy * PIXELS_PER_THREAD);

    for (int j = 0; j < j_stop; j++) {
        sum[j] = 0.0f;
    }

    for (int proj = 0; proj < n_projections; proj++) {
        cos_angle = cos_lut[proj];
        sin_angle = sin_lut[proj];
        tmp = axis_pos + bx * cos_angle - 0.5f;

        for (int j = 0; j < j_stop; j++) {
            idy = gy * PIXELS_PER_THREAD + j;
            by = idy - axis_pos + y_offset + 0.5f;
            xf = modf (tmp + by * sin_angle, &xif);
            xi = (int) xif;

            if (j == 0) {
                /* Initialization: load four neighbors and compute central
                 * difference derivatives. */
                xi_last = xi;
                for (int i = 0; i < 4; i++) {
                    sino_local[i] = sinogram[proj * sino_width + clamp (xi - 1 + i, 0, sino_width - 1)];
                }
                d_1 = sino_local[2] - sino_local[0];
                d_2 = sino_local[3] - sino_local[1];
            }
            if (xi > xi_last) {
                /* Difference to the pixel below can be at most 1 when angle is
                 * 90 degrees, otherwise it is definitely less than one. This is
                 * the case when shift is positive, so we replace the right
                 * pixel. We shift all pixels and load a new one, this is way
                 * faster than ring buffer rotation. */
                sino_local[0] = sino_local[1];
                sino_local[1] = sino_local[2];
                sino_local[2] = sino_local[3];
                sino_local[3] = sinogram[proj * sino_width + clamp (xi + 2, 0, sino_width - 1)];
                /* Recalculate derivatives */
                d_1 = sino_local[2] - sino_local[0];
                d_2 = sino_local[3] - sino_local[1];
            }
            if (!between_0_180 && xi < xi_last) {
                /* This is for angles between 180 - 360 degrees, where the shift
                 * is negative. */
                sino_local[3] = sino_local[2];
                sino_local[2] = sino_local[1];
                sino_local[1] = sino_local[0];
                sino_local[0] = sinogram[proj * sino_width + clamp (xi - 1, 0, sino_width - 1)];
                /* Recalculate derivatives */
                d_1 = sino_local[2] - sino_local[0];
                d_2 = sino_local[3] - sino_local[1];
            }

            sum[j] +=
                xf * xf * xf * (2 * (sino_local[1] - sino_local[2]) + d_1 + d_2)
                + xf * xf * (3 * (sino_local[2] - sino_local[1]) - 2 * d_1 - d_2)
                + xf * d_1 + sino_local[1];
            xi_last = xi;
        }
    }

    for (int j = 0; j < j_stop; j++) {
        idy = gy * PIXELS_PER_THREAD + j;
        slice[idy * width + idx] = sum[j] * M_PI_F / n_projections;
    }
}
