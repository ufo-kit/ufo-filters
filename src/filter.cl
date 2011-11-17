__kernel void filter(__global float *input, __global float *output, __global float *filter)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int id = idy*get_global_size(0) + idx;
    output[id] = input[id] * filter[idx];
}
