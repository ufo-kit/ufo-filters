#pragma OPENCL EXTENSION cl_khr_fp16 : enable

kernel void
dense_accumulate(global float *in, write_only image2d_array_t out) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int size_x = get_global_size(0);
    const int size_y = get_global_size(1);
    const int proj_offset = idz * size_x * (4 * size_y);
    const int flat_y = 4 * idy;
    const int flat_idx0 = proj_offset + (flat_y + 0) * size_x + idx; 
    const int flat_idx1 = proj_offset + (flat_y + 1) * size_x + idx; 
    const int flat_idx2 = proj_offset + (flat_y + 2) * size_x + idx; 
    const int flat_idx3 = proj_offset + (flat_y + 3) * size_x + idx; 
    float4 pixel = (float4)(in[flat_idx0], in[flat_idx1], in[flat_idx2], in[flat_idx3]);
    write_imagef(out, (int4)(idx, idy, idz, 0), pixel);
}

kernel void
accumulate(
    global float *in,
    write_only image2d_array_t out,
    const int row_start,
    const int row_step,
    const int projection_height) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int size_x = get_global_size(0);
    // Projection offset is calculated using the idz (index of the projection in its batch), means
    // with this offset a new projection starts in the flat array for a batch of projections.
    const int proj_offset = idz * size_x * projection_height;
    const int flat_y = 4 * idy;
    // Row_i points to the strided input rows. Each of the [(flat_y + i) * row_step] marks increasing
    // offsets between the strided four rows. Adding these offsets to the row_start give starting
    // indices of the strided rows. Total number of rows to be processed in this way is controlled by
    // the global work size.
    int row_0 = row_start + (flat_y + 0) * row_step;
    int row_1 = row_start + (flat_y + 1) * row_step;
    int row_2 = row_start + (flat_y + 2) * row_step;
    int row_3 = row_start + (flat_y + 3) * row_step;
    // Adding each (row_i * size_x) to projection_offset provides the final starting index of the
    // rows to be processed in the flat array for the batch.
    float val_0 = (row_0 < projection_height) ? in[proj_offset + (row_0 * size_x) + idx] : 0.0f;
    float val_1 = (row_1 < projection_height) ? in[proj_offset + (row_1 * size_x) + idx] : 0.0f;
    float val_2 = (row_2 < projection_height) ? in[proj_offset + (row_2 * size_x) + idx] : 0.0f;
    float val_3 = (row_3 < projection_height) ? in[proj_offset + (row_3 * size_x) + idx] : 0.0f;
    float4 pixel = (float4)(val_0, val_1, val_2, val_3);
    write_imagef(out, (int4)(idx, idy, idz, 0), pixel);
}

kernel void
backproject(
    read_only image2d_array_t projections,
    global float4 *slices,
    constant float *cos_lut,
    constant float *sin_lut,
    const float axis,
    const uint burst,
    sampler_t sampler) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int width = get_global_size(0);
    const int height = get_global_size(1);
    const float acx = idx - axis + 0.5f;
    const float acy = idy - axis + 0.5f;
    float4 sum = 0.0f;
    for (int proj = 0; proj < burst; proj++) {    
        float roh = axis + (acx * cos_lut[proj] + acy * sin_lut[proj]);
        sum += read_imagef(projections, sampler, (float4)(roh, idz + 0.5f, proj, 0));
    }
    slices[(idz * width * height) + (idy * width + idx)] += sum;
}

kernel void
distribute(global float4 *in, global float *out) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int width = get_global_size(0);
    const int height = get_global_size(1);
    const int base_index = idy * width + idx;
    float4 values = in[(idz * width * height) + base_index];
    out[((4 * idz + 0) * width * height) + base_index] = values.x;
    out[((4 * idz + 1) * width * height) + base_index] = values.y;
    out[((4 * idz + 2) * width * height) + base_index] = values.z;
    out[((4 * idz + 3) * width * height) + base_index] = values.w;
}
