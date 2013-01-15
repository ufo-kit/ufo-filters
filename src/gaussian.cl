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

__kernel void
h_gaussian (__global float *input,
            __global float *output,
            __constant float *weights,
            int half_num_weights)
{
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    const int width = get_global_size(0);
    const int height = get_global_size(1);

    const int from = max(0, x - half_num_weights);
    const int to = min(width, x + half_num_weights);
    float sum = 0.0f;

    for (int i = from; i < to; i++)
        sum += input[y * width + i] * weights[i - from];

    output[y * width + x] = sum;
}

__kernel void
v_gaussian (__global float *input,
            __global float *output,
            __constant float *weights,
            int half_num_weights)
{
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    const int width = get_global_size(0);
    const int height = get_global_size(1);

    const int from = max(0, y - half_num_weights);
    const int to = min(height, y + half_num_weights);
    float sum = 0.0f;
    for (int i = from; i < to; i++)
        sum += input[i * width + x] * weights[i - from];

    output[y * width + x] = sum;
}
