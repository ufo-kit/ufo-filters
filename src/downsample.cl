__kernel void
downsample_fast(__global float *input,
                __global float *output,
                unsigned int factor)
{
    /* Assuming the input frequency is evenly divisible by the integer factor,
       we can just output every Nth pixel */
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int width = get_global_size (0);

    output[idy * width + idx] = input[factor * idy * width + factor * idx];
}
