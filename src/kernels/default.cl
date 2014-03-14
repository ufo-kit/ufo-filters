__kernel void
convert_u8 (__global uchar *input,
            __global float *output)
{
    int idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
    output[idx] = (float) input[idx];
}

__kernel void
convert_u16 (__global ushort *input,
             __global float *output)
{
    int idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
    output[idx] = (float) input[idx];
}
