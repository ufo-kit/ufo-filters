kernel void
binning (global float *input,
         global float *output,
         const unsigned size,
         const unsigned in_width,
         const unsigned in_height)
{
    const size_t idx = get_global_id (0);
    const size_t idy = get_global_id (1);
    const size_t width = get_global_size (0);

    size_t index = (idy * size) * in_width + (idx * size);
    float sum;

    /* no loops for most common path, giving about 35% speed up */
    if (size == 2) {
        sum = input[index] + input[index + 1] +
              input[index + in_width] + input[index + in_width + 1];
    }
    else {
        sum = 0.0f;

        for (size_t j = 0; j < size; j++) {
            for (size_t i = 0; i < size; i++) {
                sum += input[index];
                index++;
            }

            /* next row */
            index += in_width - size;
        }
    }

    output[idy * width + idx] = sum;
}
