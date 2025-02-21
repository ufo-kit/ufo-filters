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

#define COMMON_SETUP                                                                                    \
    int stride_x = 2 * get_global_size (0);                                                             \
    int stride_y = stride_x * get_global_size (1);                                                      \
    int idx = get_global_id (2) * stride_y + get_global_id (1) * stride_x + 2 * get_global_id (0);


kernel void
c_add (global float *in1,
       global float *in2,
       global float *out)
{
    COMMON_SETUP;

    out[idx] = in1[idx] + in2[idx];
    out[idx+1] = in1[idx+1] + in2[idx+1];
}

kernel void
c_mul (global float *in1,
       global float *in2,
       global float *out)
{
    COMMON_SETUP;
    const float a = in1[idx];
    const float b = in1[idx+1];
    const float c = in2[idx];
    const float d = in2[idx+1];

    out[idx] = a*c - b*d;
    out[idx+1] = b*c + a*d;
}

kernel void
c_div (global float *in1,
       global float *in2,
       global float *out)
{
    COMMON_SETUP;
    const float a = in1[idx];
    const float b = in1[idx+1];
    const float c = in2[idx];
    const float d = in2[idx+1];
    float divisor = c*c + d*d;

    if (divisor == 0.0f)
        divisor = 0.000000001f;

    out[idx] = (a*c + b*d) / divisor;
    out[idx+1] = (b*c - a*d) / divisor;
}

kernel void
c_conj (global float *data,
        global float *out)
{
    COMMON_SETUP;
    out[idx] = data[idx];
    out[idx+1] = -data[idx+1];
}

/**
 * Compute cross-correlation in Fourier space:
 *  conj(laplace * gauss * fft2(in1)) * fft2(laplace * gauss * in2).
 *
 * @in1: fft2 of the first input
 * @in2: fft2 of the second input
 * @gauss_sigma: sigma of the Gaussian for blurring (0 - disable)
 * @apply_laplace: apply or not the Laplace operator
 * @out: complex result
 */
kernel void
c_crosscorr (global float *in1,
             global float *in2,
             const float gauss_sigma,
             const int apply_laplace,
             global float *out)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int width = get_global_size (0);
    int height = get_global_size (1);
    int index = 2 * width * idy + 2 * idx;
    float a, b, c, d, u, v, lap = 1.0f, gauss = 1.0f;

    /* Put mean to zero, easier to review results */
    if (index == 0) {
        out[index] = 0.0f;
        out[index + 1] = 0.0f;
    } else {
        if (apply_laplace || gauss_sigma > 0.0f) {
            u = (float) ((idx >= (width >> 1) ? idx - width : idx)) / width;
            v = (float) ((idy >= (height >> 1) ? idy - height : idy)) / height;
            if (apply_laplace) {
                /* Laplace operator to find edges */
                lap = 16 * M_PI_F * M_PI_F * M_PI_F * M_PI_F * (u * u + v * v) * (u * u + v * v);
            }
            if (gauss_sigma > 0.0f) {
                /* Gaussian blurring */
                gauss = exp (-4 * M_PI_F * M_PI_F * gauss_sigma * (u * u + v * v));
            }
        }

        a = in1[index];
        /* Conjugation */
        b = -in1[index + 1];
        c = in2[index];
        d = in2[index + 1];

        out[index] = gauss * lap * (a * c - b * d);
        out[index + 1] = gauss * lap * (b * c + a * d);
    }
}

/**
 * Compute cross-correlation in Fourier space at an arbitrary (non-integer) point:
 *  output(x, y) = IDFT[conj(laplace * gauss * fft2(in1)) * fft2(laplace * gauss * in2)].
 *
 * @in1: fft2 of the first input
 * @in2: fft2 of the second input
 * @x: horizontal real space coordinate
 * @y: vertical real space coordinate
 * @gauss_sigma: sigma of the Gaussian for blurring (0 - disable)
 * @apply_laplace: apply or not the Laplace operator
 * @out: real result
 */
kernel void
crosscorr_idft_2 (global float *in1,
                  global float *in2,
                  const float x,
                  const float y,
                  const float gauss_sigma,
                  const int apply_laplace,
                  global float *out)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int width = get_global_size (0);
    int height = get_global_size (1);
    int index = 2 * width * idy + 2 * idx;
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f, tr, ti, sin_angle, cos_angle, arg, lap = 1.0f, gauss = 1.0f;
    float u = (float) ((idx >= (width >> 1) ? idx - width : idx)) / width;
    float v = (float) ((idy >= (height >> 1) ? idy - height : idy)) / height;

    if (apply_laplace) {
        /* Laplace operator to find edges */
        lap = 16 * M_PI_F * M_PI_F * M_PI_F * M_PI_F * (u * u + v * v) * (u * u + v * v);
    }
    if (gauss_sigma > 0.0f) {
        /* Gaussian blurring */
        gauss = exp (-4 * M_PI_F * M_PI_F * gauss_sigma * (u * u + v * v));
    }

    if (index) {
        /* Put mean to zero (index == 0), easier to review results */
        a = in1[index];
        /* Conjugation */
        b = -in1[index + 1];
        c = in2[index];
        d = in2[index + 1];
    }

    tr = gauss * lap * (a * c - b * d);
    ti = gauss * lap * (b * c + a * d);

    arg = 2 * M_PI_F * (u * x + v * y);
    sin_angle = sincos (arg, &cos_angle);

    out[idy * width + idx] = tr * cos_angle - ti * sin_angle;
}

/**
 * Compute power spectrum.
 *
 * @data: complex input
 * @out: real output
 */
kernel void
c_abs_squared (global float *data,
               global float *out)
{
    int width = get_global_size (0);
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int out_idx = width * idy + idx;
    /* Input data must be complex interleaved, global dimensions are with
     * respect to the real output, so multiply width by 2. */
    int in_idx = 2 * width * idy + 2 * idx;

    out[out_idx] = data[in_idx] * data[in_idx] + data[in_idx + 1] * data[in_idx + 1];
}

/**
  * c_mul_real_sym:
  * @frequencies: complex Fourier transform frequencies with interleaved
  * real/imaginary values
  * @output: multiplication result
  * @coefficients: first half of symmetric coefficients for the multiplication
  * (size = width / 2 + 1)
  *
  * Multiply every row of @frequencies with @coefficients which are half the *real*
  * width + 1, i.e. width = global size / 2 because of the complex numbers. This
  * kernel takes advantage of symmetry and expects @frequencies to be ordered as
  * [0, 1, ..., width / 2 - 1, -width / 2, ..., -1]. After width / 2 the @coefficients
  * are mirrored.
  */
kernel void
c_mul_real_sym (global float *frequencies,
                global float *output,
                constant float *coefficients)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int real_width = get_global_size (0) / 2;
    const int index = idy * 2 * real_width + idx;
    const int real_index = idx < real_width ? idx / 2 : real_width - idx / 2;

    output[index] = frequencies[index] * coefficients[real_index];
}
