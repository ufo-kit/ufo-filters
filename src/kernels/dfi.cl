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

#include <dfi-sinc.h>

constant sampler_t image_sampler_ktbl = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_LINEAR;
constant sampler_t image_sampler_data = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST;

#define PI 3.1415926535897932384626433832795028841971693993751058209749445923078164062f

typedef struct {
        float real;
        float imag;
} clFFT_Complex;

kernel void
clear_kernel(global clFFT_Complex *output) {
    int global_x_size = get_global_size(0);
    int local_x_id = get_global_id(0);
    int local_y_id = get_global_id(1);

    output[local_y_id * global_x_size + local_x_id].real = 0.0f;
    output[local_y_id * global_x_size + local_x_id].imag = 0.0f;
}

kernel void
dfi_sinc_kernel(read_only image2d_t input, 
                read_only image2d_t ktbl,
                const DfiSincData dfi_data,
                global clFFT_Complex *output)
{
    const float2 out_coord;
    out_coord.x = get_global_id(0) + dfi_data.spectrum_offset;
    out_coord.y = get_global_id(1) + dfi_data.spectrum_offset;

    const float  half_raster_size = dfi_data.raster_size * 0.5f;
    const float2 norm_gl_coord;
    norm_gl_coord.x = out_coord.x - half_raster_size;
    norm_gl_coord.y = out_coord.y - half_raster_size;

    // calculate coordinates
    float radius = hypot(norm_gl_coord.x, norm_gl_coord.y);

    if (radius > dfi_data.radius_max)
        return;

    float2 in_coord;
    in_coord.y = atan2(norm_gl_coord.y, norm_gl_coord.x);
    in_coord.y = -in_coord.y; // spike here! (mirroring along y-axis)

    const int sign = (in_coord.y < 0.0) ? -1 : 1;

    in_coord.y = (in_coord.y < 0.0f) ? (in_coord.y += PI) : in_coord.y;
    in_coord.y = (float) min (1.0f + in_coord.y * dfi_data.inv_angle_step_rad,
                              dfi_data.theta_max - 1);
    in_coord.x = (float) min (radius, half_raster_size);

    // sinc interpolation
    int    iul, iuh, ivl, ivh, i, j, k;

    iul = (int)ceil(in_coord.x - dfi_data.half_kernel_length);
    iul = (iul < 0) ? 0 : iul;

    iuh = (int)floor(in_coord.x + dfi_data.half_kernel_length);
    iuh = (iuh > dfi_data.rho_max - 1) ? iuh = dfi_data.rho_max - 1 : iuh;

    ivl = (int)ceil(in_coord.y - dfi_data.half_kernel_length);
    ivl = (ivl < 0) ? 0 : ivl;

    ivh = (int)floor(in_coord.y + dfi_data.half_kernel_length);
    ivh = (ivh > dfi_data.theta_max - 1) ? ivh = dfi_data.theta_max - 1 : ivh;

    float weight, kernel_x_val;
    float res_real      = 0.0f;
    float res_imag      = 0.0f;

    float2 id_data_coord;
    float2 ktbl_coord;
    ktbl_coord.y = 0.5f;

    float kernel_y[20];

    for (i = ivl, j = 0; i <= ivh; ++i, ++j) {
        ktbl_coord.x = dfi_data.half_ktbl_length + (in_coord.y - i) * dfi_data.table_spacing;
        kernel_y[j] = read_imagef(ktbl, image_sampler_ktbl, ktbl_coord).s0;
    }

    for (i = iul; i <= iuh; ++i) {
        ktbl_coord.x = dfi_data.half_ktbl_length + (in_coord.x - i) * dfi_data.table_spacing;
        kernel_x_val = read_imagef(ktbl, image_sampler_ktbl, ktbl_coord).s0;

        for (k = ivl, j = 0; k <= ivh; ++k, ++j) {
            weight = kernel_y[j] * kernel_x_val;

            id_data_coord.x = i;
            id_data_coord.y = k;

            float4 cpxl = read_imagef(input, image_sampler_data, id_data_coord);

            res_real += cpxl.x * weight;
            res_imag += cpxl.y * weight;
        }
    }

    const long out_idx = out_coord.y * dfi_data.raster_size + out_coord.x;
    output[out_idx].real = res_real;
    output[out_idx].imag = sign * res_imag;
}
