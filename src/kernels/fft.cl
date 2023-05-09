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

kernel void
fft_spread (global float *out,
            global float *in,
            const int width,
            const int height,
            const int depth)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int len_x = get_global_size(0);
    const int len_y = get_global_size(1);

    const int stride_x = len_x * 2;
    const int stride_y = stride_x * len_y;
    const int stride_x_in = width;
    const int stride_y_in = stride_x_in * height;

    /* May diverge but not possible to reduce latency, because num_bins can
       be arbitrary and not be aligned. */
    if (idy >= height || idx >= width || idz >= depth) {
        out[idz*stride_y + idy*stride_x + idx*2] = 0.0f;
        out[idz*stride_y + idy*stride_x + idx*2 + 1] = 0.0f;
    }
    else {
        out[idz*stride_y + idy*stride_x + idx*2] = in[idz*stride_y_in + idy*stride_x_in + idx];
        out[idz*stride_y + idy*stride_x + idx*2 + 1] = 0.0f;
    }
}

kernel void
fft_pack (global float *in,
          global float *out,
          const int width,
          const int height,
          const float scale,
          const int is_complex)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);

    const int len_x = get_global_size(0);
    const int len_y = get_global_size(1);

    const int stride_x_in = len_x * 2;
    const int stride_y_in = len_y * stride_x_in;

    int stride_x = width;
    if (is_complex) {
        stride_x *= 2;
    }
    const int stride_y = height * stride_x;

    if (idx  < width && idy < height) {
        if (is_complex) {
            out[idz*stride_y + idy*stride_x + idx*2] = in[idz*stride_y_in + idy*stride_x_in + idx*2] * scale;
            out[idz*stride_y + idy*stride_x + idx*2 + 1] = in[idz*stride_y_in + idy*stride_x_in + idx*2 + 1] * scale;
        } else {
            out[idz*stride_y + idy*stride_x + idx] = in[idz*stride_y_in + idy*stride_x_in + idx*2] * scale;
        }
    }
}

kernel void
fft_normalize (global float *data)
{
    const int idx = get_global_id(0);
    const int dim_fft = get_global_size(0);
    data[2*idx] = data[2*idx] / dim_fft;
}

/**
 * Compute Chirp-z coefficients and also 1 / coeffs, i.e. the conjugate.
 */
kernel void
fft_compute_chirp_coeffs (global float *coeffs,
                          global float *conj_coeffs,
                          const int orig_width,
                          const int orig_height,
                          const int orig_depth,
                          const unsigned int num_dims)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int width = get_global_size (0);
    const int height = get_global_size (1);
    const int depth = get_global_size (2);
    const int even_index = idz * height * width * 2 + idy * width * 2 + 2 * idx;
    float x = (idx >=  width >> 1) ? idx -  width : idx;
    float y = (idy >= height >> 1) ? idy - height : idy;
    float z = (idz >=  depth >> 1) ? idz -  depth : idz;
    float arg;

    switch (num_dims) {
        case 1:
             arg = -M_PI_F * (x * x / orig_width);
             break;
        case 2:
             arg = -M_PI_F * (x * x / orig_width + y * y / orig_height);
             break;
        case 3:
             arg = -M_PI_F * (x * x / orig_width + y * y / orig_height + z * z / orig_depth);
             break;
    }

    coeffs[even_index] = cos (arg);
    coeffs[even_index + 1] = sin (arg);
    conj_coeffs[even_index] = coeffs[even_index];
    conj_coeffs[even_index + 1] = -coeffs[even_index + 1];
}

kernel void
fft_multiply_chirp_coeffs (global float *out,
                           global float *data,
                           global float *coeffs,
                           const int intermediate_width,
                           const int intermediate_height,
                           const int is_input_complex)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int width = get_global_size (0);
    const int height = get_global_size (1);
    const int depth = get_global_size (2);
    /* Stride of the coefficient array (intermediate size of the FFT) */
    const int stride_x = 2 * intermediate_width;
    const int stride_y = stride_x * intermediate_height;
    const int even_index = idz * stride_y + idy * stride_x + 2 * idx;
    int input_even_index;
    float2 value;

    if (is_input_complex) {
        input_even_index = idz * height * width * 2 + idy * width * 2 + 2 * idx;
        value.x = data[input_even_index];
        value.y = data[input_even_index + 1];
    } else {
        input_even_index = idz * height * width + idy * width + idx;
        value.x = data[input_even_index];
        value.y = 0.0f;
    }

    /* ac - bd + i(bc + ad) */
    out[even_index    ] = value.x * coeffs[even_index] - value.y * coeffs[even_index + 1];
    out[even_index + 1] = value.y * coeffs[even_index] + value.x * coeffs[even_index + 1];
}
