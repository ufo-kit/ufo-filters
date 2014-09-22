kernel void
flat_field_correct (global float *output,
                    global float *radiograph,
                    global float *dark_field,
                    global float *flat_field)
{
    int gid = get_global_id (1) * get_global_size (0) + get_global_id (0);
    output[gid] = (radiograph[gid] - dark_field[gid]) / (flat_field[gid] - dark_field[gid]);
}
