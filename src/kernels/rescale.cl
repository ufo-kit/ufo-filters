kernel void
downsample_fast(global float *input,
                global float *output,
                unsigned int x_factor,
                unsigned int y_factor)
{
    /* Assuming the input frequency is evenly divisible by the integer factor,
       we can just output every Nth pixel */
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int width = get_global_size (0);

    output[idy * width + idx] = input[x_factor * y_factor * idy * width + x_factor * idx];
}

kernel void rescale (read_only image2d_t input,
                     global float *output,
                     const sampler_t sampler,
                     const float x_factor,
                     const float y_factor)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);

    output[idy * get_global_size(0) + idx] = read_imagef(input, sampler,
                                                         (float2) (idx / x_factor, idy / y_factor)).x;
}
