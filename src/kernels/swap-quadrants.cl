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
 
typedef struct {
        float real;
        float imag;
} clFFT_Complex;

kernel void
swap_quadrants_kernel_real(global float *input,
                           global float *output,
                           const int width,
                           const int height,
                           const int width_mod,
                           const int height_mod)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int width_2 = width / 2;
    int height_2 = height / 2;

    // Bottom left -> Top right
    output[(idy + height_2) * width + idx + width_2] = input[idy * width + idx];
    if (idy < height_2) {
        // Top left -> bottom right
        output[idy * width + idx + width_2] = input[(idy + height_2 + height_mod) * width + idx];
    }
    if (idx < width_2) {
        // Bottom right -> top left
        output[(idy + height_2) * width + idx] = input[idy * width + idx + width_2 + width_mod];
    }
    if (idx < width_2 && idy < height_2) {
        // Top right -> bottom left
        output[idy * width + idx] = input[(idy + height_2 + height_mod) * width + idx + width_2 + width_mod];
    }
}

kernel void
swap_quadrants_kernel_complex(global clFFT_Complex *input,
                              global clFFT_Complex *output,
                              const int width,
                              const int height,
                              const int width_mod,
                              const int height_mod)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int width_2 = width / 2;
    int height_2 = height / 2;

    // Bottom left -> Top right
    output[(idy + height_2) * width + idx + width_2] = input[idy * width + idx];
    if (idy < height_2) {
        // Top left -> bottom right
        output[idy * width + idx + width_2] = input[(idy + height_2 + height_mod) * width + idx];
    }
    if (idx < width_2) {
        // Bottom right -> top left
        output[(idy + height_2) * width + idx] = input[idy * width + idx + width_2 + width_mod];
    }
    if (idx < width_2 && idy < height_2) {
        // Top right -> bottom left
        output[idy * width + idx] = input[(idy + height_2 + height_mod) * width + idx + width_2 + width_mod];
    }
}
