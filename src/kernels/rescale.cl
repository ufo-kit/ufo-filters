kernel void rescale (read_only image2d_t input,
                     global float *output,
                     const sampler_t sampler,
                     const float x_factor,
                     const float y_factor)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);

    output[idy * get_global_size(0) + idx] = read_imagef(input, sampler,
                                                         (float2) (idx / x_factor + 0.5f,
                                                                   idy / y_factor + 0.5f)).x;
}
