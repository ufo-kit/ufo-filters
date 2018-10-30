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

#define flatten(x,y,r,w) ((y-r)*w + (x-r))

/* Compute the distance of two neighbourhood vectors _starting_ from index i
   and j and edge length radius */
float
dist (global float *input,
      int i,
      int j,
      int radius,
      int image_width)
{
    float dist = 0.0f, tmp;
    float wsize = (2.0f * radius + 1.0f);
    wsize *= wsize;

    const int nb_width = 2 * radius + 1;
    const int stride = image_width - nb_width;
    for (int k = 0; k < nb_width; k++, i += stride, j += stride) {
        for (int l = 0; l < nb_width; l++, i++, j++) {
            tmp = input[i] - input[j];
            dist += tmp * tmp;
        }
    }
    return dist / wsize;
}

kernel void
nlm_noise_reduction (global float *input,
                     global float *output,
                     const int search_radius,
                     const int patch_radius,
                     const float sigma)
{
    const int x = get_global_id (0);
    const int y = get_global_id (1);
    const int width = get_global_size (0);
    const int height = get_global_size (1);
    const float sigma_2 = sigma * sigma;

    float total_weight = 0.0f;
    float pixel_value = 0.0f;

    /* 
     * Compute the upper left (sx,sy) and lower right (tx, ty) corner points of
     * our search window.
     */
    int r = min (patch_radius, min(width - 1 - x, min (height - 1 - y, min (x, y))));
    int sx = max (x - search_radius, r);
    int sy = max (y - search_radius, r);
    int tx = min (x + search_radius, width - 1 - r);
    int ty = min (y + search_radius, height - 1 - r);

    for (int i = sx; i < tx; i++) {
        for (int j = sy; j < ty; j++) {
            float d = dist (input, flatten(x, y, r, width), flatten (i,j,r,width), r, width);
            float weight = exp (- sigma_2 * d);
            pixel_value += weight * input[j * width + i];
            total_weight += weight;
        }
    }

    output[y * width + x] = pixel_value / total_weight;
}
