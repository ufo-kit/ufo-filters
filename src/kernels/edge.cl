/*
 * Copyright (C) 2015-2016 Karlsruhe Institute of Technology
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

kernel void
filter (read_only image2d_t input,
        constant float *mask,
        sampler_t sampler,
        const char second_pass,
        global float *output)
{
    int2 here = (int2) (get_global_id (0), get_global_id (1));
    int2 shape = (int2) (get_global_size (0), get_global_size (1));
    float2 norm_here = (float2) (
        ((float) here.x + 0.5f) / ((float) shape.x),
        ((float) here.y + 0.5f) / ((float) shape.y)
    );
    const float2 delta = (float2) (
        1.0f / ((float) shape.x),
        1.0f / ((float) shape.y)
    );
    float data[9];
    float result;

    /*
     * Note: using local memory to access the mask does not improve performance.
     * Storing image data may be though. At the moment we can compute ~3
     * Gigapixel/s with two passes.
     */

    data[0] = read_imagef (input, sampler, (float2) (norm_here.x - delta.x, norm_here.y - delta.y)).x;
    data[1] = read_imagef (input, sampler, (float2) (norm_here.x + 0,       norm_here.y - delta.y)).x;
    data[2] = read_imagef (input, sampler, (float2) (norm_here.x + delta.x, norm_here.y - delta.y)).x;
    data[3] = read_imagef (input, sampler, (float2) (norm_here.x - delta.x, norm_here.y + 0)).x;
    data[4] = read_imagef (input, sampler, (float2) (norm_here.x + 0,       norm_here.y + 0)).x;
    data[5] = read_imagef (input, sampler, (float2) (norm_here.x + delta.x, norm_here.y + 0)).x;
    data[6] = read_imagef (input, sampler, (float2) (norm_here.x - delta.x, norm_here.y + delta.y)).x;
    data[7] = read_imagef (input, sampler, (float2) (norm_here.x + 0,       norm_here.y + delta.y)).x;
    data[8] = read_imagef (input, sampler, (float2) (norm_here.x + delta.x, norm_here.y + delta.y)).x;

    for (int i = 0; i < 9; i++)
        result += data[i] * mask[i];

    if (second_pass) {
        float second;

        second = mask[6] * data[0] + mask[3] * data[1] + mask[0] * data[2] +
                 mask[7] * data[0] + mask[4] * data[1] + mask[1] * data[2] +
                 mask[8] * data[0] + mask[5] * data[1] + mask[2] * data[2];

        result = sqrt (result * result + second * second);
    }

    output[here.y * shape.x + here.x] = result;
}
