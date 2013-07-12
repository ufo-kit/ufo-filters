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


__kernel void zeropadding_kernel(__global float *input, int sino_width, __global clFFT_Complex *output)
{
	const uint global_x_size = get_global_size(0);

	int lpart = sino_width/2;
	int rpart = sino_width/2;
	int global_x_size2 = global_x_size/2;
	int len_to_lpart = global_x_size - lpart;

	int g_idx = get_global_id(0);
	int g_idy = get_global_id(1);

	output[g_idy * global_x_size + g_idx].real = 
					(g_idx < rpart) ? input[g_idy * sino_width + (lpart + g_idx)] : 
									  (g_idx < (global_x_size2 + (global_x_size2 - lpart))) ? 0.0f : 
																	  	  input[g_idy * sino_width + (g_idx - len_to_lpart)];
	output[g_idy * global_x_size + g_idx].imag = 0.0f;
}