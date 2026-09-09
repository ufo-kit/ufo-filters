/*
 * Copyright (C) 2011-2017 Karlsruhe Institute of Technology
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
accumulate(
    global float *in,
    write_only image2d_array_t out,
    const int row_start,
    const int row_step,
    const int projection_height,
    const int projection_width) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int size_x = projection_width;
    if (idx >= size_x)
        return;
    // Projection offset is calculated using the idz (index of the projection in its batch), means
    // with this offset a new projection starts in the flat array for a batch of projections.
    const int proj_offset = idz * size_x * projection_height;
    const int flat_y = 4 * idy;
    // row_i points to the strided input rows. Each of the [(flat_y + i) * row_step] marks increasing
    // offsets between the strided four rows. Adding these offsets to the row_start give starting
    // indices of the strided rows. Total number of rows to be processed in this way is controlled by
    // the global work size.
    int row_0 = row_start + (flat_y + 0) * row_step;
    int row_1 = row_start + (flat_y + 1) * row_step;
    int row_2 = row_start + (flat_y + 2) * row_step;
    int row_3 = row_start + (flat_y + 3) * row_step;
    // Adding each (row_i * size_x) to projection_offset provides the final starting index of the
    // rows to be processed in the flat array for the batch.
    float val_0 = (row_0 >= 0 && row_0 < projection_height) ? in[proj_offset + (row_0 * size_x) + idx] : 0.0f;
    float val_1 = (row_1 >= 0 && row_1 < projection_height) ? in[proj_offset + (row_1 * size_x) + idx] : 0.0f;
    float val_2 = (row_2 >= 0 && row_2 < projection_height) ? in[proj_offset + (row_2 * size_x) + idx] : 0.0f;
    float val_3 = (row_3 >= 0 && row_3 < projection_height) ? in[proj_offset + (row_3 * size_x) + idx] : 0.0f;
    float4 pixel = (float4)(val_0, val_1, val_2, val_3);
    write_imagef(out, (int4)(idx, idy, idz, 0), pixel);
}

kernel void
backproject(
    read_only image2d_array_t projections,
    global float4 *slices,
    constant float2 *angle_lut,
    const float axis,
    const uint burst,
    sampler_t sampler,
    const int slice_width,
    const int slice_height,
    const float2 x_region,
    const float2 y_region,
    const uint first_burst) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const float volume_x = mad ((float) idx, x_region.y, x_region.x);
    const float volume_y = mad ((float) idy, y_region.y, y_region.x);
    float4 sum = 0.0f;
    for (int proj = 0; proj < burst; proj++) {
        // angle_lut is an array of two ordered floating point values, denoting the cosine and sine
        // of rotation angles.
        const float2 angle = angle_lut[proj];
        float roh = axis + (volume_x * angle.x + volume_y * angle.y);
        sum += read_imagef(projections, sampler, (float4)(roh, idz + 0.5f, proj, 0));
    }
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t output_index = ((size_t) idz * plane) + ((size_t) idy * slice_width + idx);
    if (first_burst)
        slices[output_index] = sum;
    else
        slices[output_index] += sum;
}

kernel void
backproject_even(
    read_only image2d_array_t projections,
    global float4 *slices,
    constant float2 *angle_lut,
    const float axis,
    const uint burst,
    sampler_t sampler,
    const int slice_width,
    const int slice_height,
    const float2 x_region,
    const float2 y_region,
    const uint first_burst) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const float volume_x = mad ((float) idx, x_region.y, x_region.x);
    const float volume_y = mad ((float) idy, y_region.y, y_region.x);
    float4 sum = 0.0f;
    for (uint proj = 0; proj < burst; proj += 2) {
        const float2 angle = angle_lut[proj];
        const float roh = axis + (volume_x * angle.x + volume_y * angle.y);
        sum += read_imagef(projections, sampler, (float4)(roh, idz + 0.5f, proj, 0));
    }
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t output_index = ((size_t) idz * plane) + ((size_t) idy * slice_width + idx);
    if (first_burst)
        slices[output_index] = sum;
    else
        slices[output_index] += sum;
}

kernel void
backproject_odd(
    read_only image2d_array_t projections,
    global float4 *slices,
    constant float2 *angle_lut,
    const float axis,
    const uint burst,
    sampler_t sampler,
    const int slice_width,
    const int slice_height,
    const float2 x_region,
    const float2 y_region,
    const uint first_burst) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const float volume_x = mad ((float) idx, x_region.y, x_region.x);
    const float volume_y = mad ((float) idy, y_region.y, y_region.x);
    float4 sum = 0.0f;
    for (uint proj = 1; proj < burst; proj += 2) {
        const float2 angle = angle_lut[proj];
        const float roh = axis + (volume_x * angle.x + volume_y * angle.y);
        sum += read_imagef(projections, sampler, (float4)(roh, idz + 0.5f, proj, 0));
    }
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t output_index = ((size_t) idz * plane) + ((size_t) idy * slice_width + idx);
    if (first_burst)
        slices[output_index] = sum;
    else
        slices[output_index] += sum;
}

kernel void
distribute(
    global float4 *in,
    global float *out,
    const int slice_width,
    const int slice_height,
    const float normalization_factor) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t base_index = (size_t) idy * slice_width + idx;
    float4 values = in[((size_t) idz * plane) + base_index] * normalization_factor;
    out[((size_t) (4 * idz + 0) * plane) + base_index] = values.x;
    out[((size_t) (4 * idz + 1) * plane) + base_index] = values.y;
    out[((size_t) (4 * idz + 2) * plane) + base_index] = values.z;
    out[((size_t) (4 * idz + 3) * plane) + base_index] = values.w;
}

kernel void
distribute_volume(
    global float4 *in,
    global float *out,
    const int slice_width,
    const int slice_height,
    const int num_slices,
    const float normalization_factor) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t base_index = (size_t) idy * slice_width + idx;
    const int slice = 4 * idz;
    float4 values = in[((size_t) idz * plane) + base_index] * normalization_factor;
    if (slice + 0 < num_slices)
        out[((size_t) (slice + 0) * plane) + base_index] = values.x;
    if (slice + 1 < num_slices)
        out[((size_t) (slice + 1) * plane) + base_index] = values.y;
    if (slice + 2 < num_slices)
        out[((size_t) (slice + 2) * plane) + base_index] = values.z;
    if (slice + 3 < num_slices)
        out[((size_t) (slice + 3) * plane) + base_index] = values.w;
}
