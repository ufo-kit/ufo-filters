kernel void
convert_u8 (global uchar *input,
            global float *output)
{
    int idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
    output[idx] = (float) input[idx];
}

kernel void
convert_u16 (global ushort *input,
             global float *output)
{
    int idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
    output[idx] = (float) input[idx];
}

kernel void
convert_u8_1d (global uchar *input,
            global float *output)
{
    int idx = get_global_id (0);
    output[idx] = (float) input[idx];
}

kernel void
convert_u16_1d (global ushort *input,
             global float *output)
{
    int idx = get_global_id (0);
    output[idx] = (float) input[idx];
}
