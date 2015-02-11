kernel
void mult(global float* a, global float* b, global float* res)
{
    int width = get_global_size(0) * 2;
    int x = get_global_id(0) * 2;
    int y = get_global_id(1);
    int idx_r = x + y * width;
    int idx_i = 1 + x + y * width;

    float ra = a[idx_r];
    float rb = b[idx_r];
    float ia = a[idx_i];
    float ib = b[idx_i];

    res[idx_r] = ra * rb - ia * ib;
    res[idx_i] = ra * ib + rb * ia;
}
