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


enum {
    UFO_FINITE_DIFFERENCE_FORWARD,
    UFO_FINITE_DIFFERENCE_BACKWARD,
    UFO_FINITE_DIFFERENCE_CENTRAL,
};

float get_pixel (read_only image2d_t input,
                 sampler_t sampler,
                 int x,
                 int y)
{
    float2 norm_pixel = (float2) (((float) x + 0.5f) / get_image_width (input),
                                  ((float) y + 0.5f) / get_image_height (input));

    return read_imagef (input, sampler, norm_pixel).x;
}

float compute_finite_difference (read_only image2d_t input,
                                 sampler_t sampler,
                                 int finite_difference_type,
                                 int idx,
                                 int idy,
                                 int dx,
                                 int dy)
{
    float result;

    switch (finite_difference_type) {
        case UFO_FINITE_DIFFERENCE_FORWARD:
            result = get_pixel (input, sampler, idx + dx, idy + dy) - get_pixel (input, sampler, idx, idy);
            break;
        case UFO_FINITE_DIFFERENCE_BACKWARD:
            result = get_pixel (input, sampler, idx, idy) - get_pixel (input, sampler, idx - dx, idy - dy);
            break;
        case UFO_FINITE_DIFFERENCE_CENTRAL:
            result =  0.5f * (get_pixel (input, sampler, idx + dx, idy + dy) - get_pixel (input, sampler, idx - dx, idy - dy));
            break;
        default:
            result = NAN;
            break;
    }

    return result;
}

float compute_horizontal (read_only image2d_t input,
                          sampler_t sampler,
                          int finite_difference_type,
                          int idx,
                          int idy)
{
    return compute_finite_difference (input, sampler, finite_difference_type, idx, idy, 1, 0);
}

float compute_vertical (read_only image2d_t input,
                        sampler_t sampler,
                        int finite_difference_type,
                        int idx,
                        int idy)
{
    return compute_finite_difference (input, sampler, finite_difference_type, idx, idy, 0, 1);
}

kernel void horizontal (read_only image2d_t input,
                        sampler_t sampler,
                        const int finite_difference_type,
                        global float *output)
{
    int width = get_global_size (0);
    int idx = get_global_id (0);
    int idy = get_global_id (1);

    output[idy * width + idx] = compute_horizontal (input, sampler, finite_difference_type, idx, idy);
}

kernel void vertical (read_only image2d_t input,
                      sampler_t sampler,
                      const int finite_difference_type,
                      global float *output)
{
    int width = get_global_size (0);
    int idx = get_global_id (0);
    int idy = get_global_id (1);

    output[idy * width + idx] = compute_vertical (input, sampler, finite_difference_type, idx, idy);
}

kernel void both (read_only image2d_t input,
                  sampler_t sampler,
                  const int finite_difference_type,
                  global float *output)
{
    int width = get_global_size (0);
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    output[idy * width + idx] = compute_horizontal (input, sampler, finite_difference_type, idx, idy) +
                                compute_vertical (input, sampler, finite_difference_type, idx, idy);
}

kernel void both_abs (read_only image2d_t input,
                      sampler_t sampler,
                      const int finite_difference_type,
                      global float *output)
{
    int width = get_global_size (0);
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    output[idy * width + idx] = fabs (compute_horizontal (input, sampler, finite_difference_type, idx, idy)) +
                                fabs (compute_vertical (input, sampler, finite_difference_type, idx, idy));
}
