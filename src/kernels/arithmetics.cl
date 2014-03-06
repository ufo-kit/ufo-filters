__kernel void
subtract (global float *a,
          global float *b,
          global float *y)
{
    int idx = get_global_id(0);
    y[idx] = a[idx] - b[idx];
}
