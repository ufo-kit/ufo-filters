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
c_add (__global float *in1,
       __global float *in2,
       __global float *out)
{
    int idx = get_global_id(1) * 2 * get_global_size(0) + 2 * get_global_id(0);

    out[idx] = in1[idx] + in2[idx];
    out[idx+1] = in1[idx+1] + in2[idx+1];
}

__kernel void
c_mul (__global float *in1,
       __global float *in2,
       __global float *out)
{
    int idx = get_global_id(1) * 2 * get_global_size(0) + 2 * get_global_id(0);
    const float a = in1[idx];
    const float b = in1[idx+1];
    const float c = in2[idx];
    const float d = in2[idx+1];

    out[idx] = a*c - b*d;
    out[idx+1] = b*c + a*d;
}

__kernel void
c_div (__global float *in1,
       __global float *in2,
       __global float *out)
{
    int idx = get_global_id(1) * 2 * get_global_size(0) + 2 * get_global_id(0);
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

__kernel void
c_conj (__global float *data)
{
    int idx = get_global_id(1) * 2 * get_global_size(0) + 2 * get_global_id(0);
    data[idx+1] = -data[idx+1];
}
