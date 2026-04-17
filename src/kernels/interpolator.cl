/*
 * Copyright (C) 2011-2025 Karlsruhe Institute of Technology
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
interpolate (global float *a,
             global float *b,
             global float *output,
             float alpha)
{
    const int index = get_global_id(1) * get_global_size(0) + get_global_id(0);
    output[index] = (1.0f - alpha) * a[index] + alpha * b[index];
}

/*
 * Interpolate two arrays along the horizontal direction, *offset* is a linear
 * offset to the first and the output arrays. In stitching, *weight* is used to
 * match the mean of the second buffer's overlapping region to the first one's.
 */
kernel void
interpolate_horizontally (global float *a,
                          global float *b,
                          global float *output,
                          const int width,
                          const int left_width,
                          const int right_width,
                          const int offset,
                          const float weight)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const float alpha = ((float) idx) / (get_global_size (0) - 1.0f);

    output[idy * width + idx + offset] = (1.0f - alpha) * a[idy * left_width + idx + offset] +
                                         alpha * weight * b[idy * right_width + idx];
}

kernel void
interpolate_mask_horizontally (global float *input,
                               global float *mask,
                               global float *output,
                               const int use_gradient)
{
   const int idx = get_global_id (0);
   const int idy = get_global_id (1);
   const int width = get_global_size (0);
   const int offset = idy * width;
   int left = idx, right = idx;
   float span, diff;

   if (mask[offset + idx]) {
        while (left > -1 && mask[offset + left] > 0) {
            left--;
        }
        while (right < width && mask[offset + right] > 0) {
            right++;
        }

        if (left < 0) {
            /* Mask spans to the left border, use only the right value */
            if (right < width - 1) {
                if (use_gradient) {
                    /* There are two valid pixels on the right, use gradient */
                    diff = input[offset + right] - input[offset + right + 1];
                    output[offset + idx] = input[offset + right] + diff * (right - idx);
                } else {
                    output[offset + idx] = input[offset + right];
                }
            } else if (right == width - 1) {
                /* There is only one valid pixel on the right, which is the only
                 * valid pixel in this row, so use it's value everywhere */
                output[offset + idx] = input[offset + right];
            } else {
                /* All pixels in this row are invalid, just copy the input */
                output[offset + idx] = input[offset + idx];
            }
        } else if (right >= width) {
            /* Mask spans to the right border, use only the left value */
            if (left > 0) {
                if (use_gradient) {
                    /* There are two valid pixels on the left, use gradient */
                    diff = input[offset + left] - input[offset + left - 1];
                    output[offset + idx] = input[offset + left] + diff * (idx - left);
                } else {
                    output[offset + idx] = input[offset + left];
                }
            } else if (left == 0) {
                /* There is only one valid pixel on the left, which is the only
                 * valid pixel in this row, so use it's value everywhere */
                output[offset + idx] = input[offset + left];
            }
        } else {
            /* There are valid pixels on both sides, use standard linear
             * interpolation */
            span = (float) (right - left);
            output[offset + idx] = (right - idx) / span * input[offset + left] +
                                   (idx - left) / span * input[offset + right];
        }
   } else {
       output[offset + idx] = input[offset + idx];
   }
}

kernel void map_coordinates (read_only image2d_t input,
                             global float *output,
                             const sampler_t sampler,
                             global float *x_positions,
                             global float *y_positions)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int width = get_global_size(0);
    int index = idy * width + idx;
    float x = x_positions[index] + 0.5f;
    float y = y_positions[index] + 0.5f;

    output[index] = read_imagef(input, sampler, (float2) (x, y)).x;
}


inline float get_image_point(global float *image, int x, int y, int width, int height)
{
    x = clamp (x, 0, width - 1);
    y = clamp (y, 0, height - 1);

    return image[y * width + x];
}


kernel void map_coordinates_cubic (global float *input,
                                   global float *output,
                                   global float *x_positions,
                                   global float *y_positions)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int width = get_global_size(0);
    int height = get_global_size(1);
    int index = idy * width + idx;
    int xi, yi, i, j;
    float xf, xif, yf, yif;
    float4 neighborhood;
    float4 psx = (float4) (0.0f, 0.0f, 0.f, 0.0f);
    float4 psy = (float4) (0.0f, 0.0f, 0.f, 0.0f);

    float4 mat[4];
    mat[0] = (float4)( 0.0f,   2.0f,   0.0f,   0.0f);
    mat[1] = (float4)(-1.0f,   0.0f,   1.0f,   0.0f);
    mat[2] = (float4)( 2.0f,  -5.0f,   4.0f,  -1.0f);
    mat[3] = (float4)(-1.0f,   3.0f,  -3.0f,   1.0f);

    float x = x_positions[index];
    float y = y_positions[index];
    xf = modf (x, &xif);
    yf = modf (y, &yif);
    xi = (int) xif;
    yi = (int) yif;
    float4 tx = (float4)(1, xf, xf * xf, xf * xf * xf);
    float4 ty = (float4)(1, yf, yf * yf, yf * yf * yf);

    for (j = yi - 1; j < yi + 3; j++) {
        neighborhood.x = get_image_point(input, xi - 1, j, width, height),
        neighborhood.y = get_image_point(input,     xi, j, width, height),
        neighborhood.z = get_image_point(input, xi + 1, j, width, height),
        neighborhood.w = get_image_point(input, xi + 2, j, width, height),
        psy.x = dot (mat[0], neighborhood);
        psy.y = dot (mat[1], neighborhood);
        psy.z = dot (mat[2], neighborhood);
        psy.w = dot (mat[3], neighborhood);
        psx[j - yi + 1] = 0.5f * dot (tx, psy);
    }

    psy.x = dot (mat[0], psx);
    psy.y = dot (mat[1], psx);
    psy.z = dot (mat[2], psx);
    psy.w = dot (mat[3], psx);

    output[index] = 0.5f * dot (ty, psy);
}
