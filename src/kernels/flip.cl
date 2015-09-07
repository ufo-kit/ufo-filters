kernel void
flip_horizontal (global float *input, global float *output)
{
    size_t idx = get_global_id (0);
    size_t idy = get_global_id (1);
    size_t width = get_global_size (0);
    size_t height = get_global_size (1);

    output[idy * width + (width - idx - 1)] = input[idy * width + idx];
}

kernel void
flip_vertical (global float *input, global float *output)
{
    size_t idx = get_global_id (0);
    size_t idy = get_global_id (1);
    size_t width = get_global_size (0);
    size_t height = get_global_size (1);

    output[(height - idy - 1) * width + idx] = input[idy * width + idx];
}
