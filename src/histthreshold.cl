
/* Naive implementation */
__kernel void histogram(__global float *input, __global float *output, unsigned int input_size, float min_range, float max_range)
{
    const int num_bins = get_global_size(0);
    const int bin = get_global_id(0);
    const float bin_width = (max_range - min_range) / num_bins;
    const float local_min = bin * bin_width;
    const float local_max = (bin + 1) * bin_width;
    float sum = 0.0f;

    for (int i = 0; i < input_size; i++) {
        if ((local_min <= input[i]) && (input[i] < local_max))
            sum += 1.0f;
    }

    output[bin] = sum;
}
