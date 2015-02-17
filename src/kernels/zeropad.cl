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
zeropadding_kernel(global float *input, int offset, int input_width, global clFFT_Complex *output)
{
    const uint sino_width = input_width - offset;
	const uint out_size = get_global_size(0);
    const uint out_size_half = out_size/2;
    const uint lpart = sino_width/2;
    const uint rpart = sino_width/2;
    const uint len = out_size_half + (out_size_half - lpart);

    const int g_idx = get_global_id(0);
	const int g_idy = get_global_id(1);

    output[g_idy * out_size + g_idx].real = 
        (g_idx < rpart) ? input[g_idy * input_width + offset + lpart + g_idx] : 
            (g_idx < len) ? 0.0f : input[g_idy * input_width + offset + (g_idx - len)];

    output[g_idy * out_size + g_idx].imag = 0.0f;
}

