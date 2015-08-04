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
            const int xdim)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int dpitch_in = get_global_size(0);
    const int dpitch = dpitch_in << 1;

    /* May diverge but not possible to reduce latency, because num_bins can
       be arbitrary and not be aligned. */
    if ((idy >= height) || ((idx % xdim) >= width)) {
        out[idy*dpitch + idx*2] = 0.0;
        out[idy*dpitch + idx*2 + 1] = 0.0;
    }
    else {
        out[idy*dpitch + idx*2] = in[idy*dpitch_in + idx];
        out[idy*dpitch + idx*2 + 1] = 0.0;
    }
}

kernel void
fft_pack (global float *in,
          global float *out,
          const int width,
          const int xdim,
          const float scale)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int batch_size = get_global_size(0) / xdim;
    const int dpitch_in = get_global_size(0) * 2;
    const int dpitch_ou = width * batch_size;

    if ((idx % xdim) < width)
        out[idy*dpitch_ou + idx] = in[idy*dpitch_in + 2*idx] * scale;
}

kernel void
fft_normalize (global float *data)
{
    const int idx = get_global_id(0);
    const int dim_fft = get_global_size(0);
    data[2*idx] = data[2*idx] / dim_fft;
}

