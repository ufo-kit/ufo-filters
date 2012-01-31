
__kernel void draw_metaballs(__global float *output, __global float2 *positions, __constant float *sizes, uint num_balls)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const float r = 1.0f;
    
    float sum = 0.0f;
    for (int i = 0; i < num_balls; i++) {
        float x = (positions[i].x - idx);
        float y = (positions[i].y - idy);
        sum += sizes[i] / sqrt(x*x + y*y);
    }
    if ((sum > (r - 0.01f)) && (sum < (r + 0.01f)))
        output[idy * get_global_size(0) + idx] = 1.0f;
    else 
        output[idy * get_global_size(0) + idx] = 0.0f;
}

