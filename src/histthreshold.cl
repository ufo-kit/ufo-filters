
/* Naive implementation */
__kernel void histogram(__global float *input, __global float *output, unsigned int input_size, float min_range, float max_range)
{
    const int num_bins = get_global_size(0);
    const int bin = get_global_id(0);
    const float bin_width = (max_range - min_range) / num_bins;
    const float local_min = bin * bin_width;
    const float local_max = (bin + 1) * bin_width;
    int sum = 0;

    for (int i = 0; i < input_size; i++) {
        if ((local_min <= input[i]) && (input[i] < local_max))
            sum++;
    }

    output[bin] = ((float) sum) / input_size;
}

__kernel void threshold(__global float *input, __global float *histogram, __global float *output)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int width = get_global_size(0);
    const float intensity = input[idy*width + idx];
    const int h_index = (int) (intensity * 256.0);

    if (histogram[h_index] < 0.03)
        output[idy*width + idx] = intensity;
    else
        output[idy*width + idx] = 0.0f;
}
