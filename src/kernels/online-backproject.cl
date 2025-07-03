#pragma OPENCL EXTENSION cl_khr_fp16 : enable
const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_LINEAR;

kernel void
accumulate(global float *in, write_only image2d_array_t out) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int size_x = get_global_size(0);
    const int size_y = get_global_size(1);
    const int flat_y = 4 * idy;
    const int proj_offset = idz * size_x * (4 * size_y);
    const int flat_idx0 = proj_offset + (flat_y + 0) * size_x + idx; 
    const int flat_idx1 = proj_offset + (flat_y + 1) * size_x + idx; 
    const int flat_idx2 = proj_offset + (flat_y + 2) * size_x + idx; 
    const int flat_idx3 = proj_offset + (flat_y + 3) * size_x + idx; 
    float4 pixel = (float4)(in[flat_idx0], in[flat_idx1], in[flat_idx2], in[flat_idx3]);
    write_imagef(out, (int4)(idx, idy, idz, 0), pixel);
}

kernel void
backproject(
    read_only image2d_array_t projections,
    global float4 *slices,
    constant float *cos_lut,
    constant float *sin_lut,
    const float axis,
    const uint burst) {
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
