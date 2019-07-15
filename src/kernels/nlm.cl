/*
 * Copyright (C) 2011-2018 Karlsruhe Institute of Technology
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

float
dist (read_only image2d_t input,
      sampler_t sampler,
      float2 p,
      float2 q,
      int radius,
      int width,
      int height,
      float variance)
{
    float dist = 0.0f, tmp;
    float wsize = (2.0f * radius + 1.0f);
    wsize *= wsize;

    for (int i = -radius; i < radius + 1; i++) {
        for (int j = -radius; j < radius + 1; j++) {
            tmp = read_imagef (input, sampler, (float2) ((p.x + i) / width, (p.y + j) / height)).x -
                    read_imagef (input, sampler, (float2) ((q.x + i) / width, (q.y + j) / height)).x;
            dist += fmax (0.0f, tmp * tmp - 2 * variance);
        }
    }

    return dist / wsize;
}

kernel void
nlm_noise_reduction (read_only image2d_t input,
                     global float *output,
                     sampler_t sampler,
                     const int search_radius,
                     const int patch_radius,
                     const float h_2,
                     const float variance)
{
    const int x = get_global_id (0);
    const int y = get_global_id (1);
    const int width = get_global_size (0);
    const int height = get_global_size (1);
    float d, weight;

    float total_weight = 0.0f;
    float pixel_value = 0.0f;

    for (int i = x - search_radius; i < x + search_radius + 1; i++) {
        for (int j = y - search_radius; j < y + search_radius + 1; j++) {
            d = dist (input, sampler, (float2) (x + 0.5f, y + 0.5f), (float2) (i + 0.5f, j + 0.5f),
                      patch_radius, width, height, variance);
            weight = exp (- h_2 * d);
            pixel_value += weight * read_imagef (input, sampler, (float2) ((i + 0.5f) / width, (j + 0.5f) / height)).x;
            total_weight += weight;
        }
    }

    output[y * width + x] = pixel_value / total_weight;
}
