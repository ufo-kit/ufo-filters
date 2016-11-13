/*
 * Copyright (C) 2014-2016 Karlsruhe Institute of Technology
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
convert_u8 (global uchar *input,
            global float *output)
{
    int idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
    output[idx] = (float) input[idx];
}

kernel void
convert_u16 (global ushort *input,
             global float *output)
{
    int idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
    output[idx] = (float) input[idx];
}

kernel void
convert_u8_1d (global uchar *input,
            global float *output)
{
    int idx = get_global_id (0);
    output[idx] = (float) input[idx];
}

kernel void
convert_u16_1d (global ushort *input,
             global float *output)
{
    int idx = get_global_id (0);
    output[idx] = (float) input[idx];
}
