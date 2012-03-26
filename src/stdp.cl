

/**
 * point_neg_ln: y = -ln(x)
 */
__kernel void point_neg_ln(__global float *input, __global float *output, __local float *shared)
{
    const int idx = get_global_id(1) * get_global_size(0) + get_global_id(0);
    output[idx] = -log(input[idx]);
}
