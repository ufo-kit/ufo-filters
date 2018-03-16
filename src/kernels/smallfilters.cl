/*
 * Copyright (C) 2013-2017 Karlsruhe Institute of Technology
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
absorptivity (global float *input,
              global float *output)
{
    int idx = get_global_id(0);
    int idy = get_global_id(1);
    int width = get_global_size(0);
    output[idy * width + idx] = - log(input[idy * width + idx]);
}

kernel void
fix_nan_inf (global float *input,
              global float *output)
{
    int idx = get_global_id(0);
    int idy = get_global_id(1);
    int width = get_global_size(0);
    float result = input[idy * width + idx];
    if ((isnan (result) || isinf (result))) {
        result = 0.0f;
    }
    output[idy * width + idx] = result;
}
