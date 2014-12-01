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
flat_correct (global float *corrected,
              global float *data,
              global const float *dark,
              global const float *flat,
              const int sinogram_input,
              const int absorptivity,
              const int fix_abnormal,
              const float dark_scale)
{
    const int gid = get_global_id(1) * get_global_size(0) + get_global_id(0);
    const int corr_idx = sinogram_input ? get_global_id(0) : gid;
    const float cdark = dark[corr_idx] * dark_scale;
    float result;

    if (absorptivity) {
        result = log ((flat[corr_idx] - cdark) / (data[gid] - cdark));
    }
    else {
        result = (data[gid] - cdark) / (flat[corr_idx] - cdark);
    }

    if (fix_abnormal && (isnan (result) || isinf (result))) {
        result = 0.0;
    }

    corrected[gid] = result;
}
