
__kernel void interpolate(__global float *a, __global float *b, __global float *output, int current, int total)
{
    const int index = get_global_id(1)*get_global_size(0) + get_global_id(0);
    float fraction = (float) current / (float) (total - 1);
    output[index] = (1.0 - fraction) * a[index] + fraction*b[index];
}
