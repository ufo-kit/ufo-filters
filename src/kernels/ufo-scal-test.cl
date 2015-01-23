const sampler_t nb_sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP |
CLK_FILTER_NEAREST;
#define EPS 0.001f
__kernel
void test (__read_only image2d_t in,
__write_only image2d_t out)
{
    const uint X = get_global_id(0);
    const uint Y = get_global_id(1);
    int2 coord_r;
    coord_r.x = get_global_id(0);
    coord_r.y = get_global_id(1);
    int2 coord_w = coord_r;
    /*
    0 1 (s-1, t+1) (s, t+1)
    2 3 4 (s-1, t) (s, t) (s+1, t)
    5 6 (s, t-1) (s+1, t-1)
    */
    for (long int i = 0; i < 100000; ++i) {
    float f[7];
    f[3] = read_imagef(in, nb_sampler, coord_r).s0;
    coord_r.y -= 1;
    f[1] = read_imagef(in, nb_sampler, coord_r).s0;
    coord_r.x -= 1;
    f[0] = read_imagef(in, nb_sampler, coord_r).s0;
    coord_r.y += 1;
    f[2] = read_imagef(in, nb_sampler, coord_r).s0;
    coord_r.x += 2;
    f[4] = read_imagef(in, nb_sampler, coord_r).s0;
    coord_r.y += 1;
    f[6] = read_imagef(in, nb_sampler, coord_r).s0;
    coord_r.x -= 1;
    f[5] = read_imagef(in, nb_sampler, coord_r).s0;
    float df =
    (2.0f * f[3] - f[2] - f[5]) /
    sqrt (EPS + (f[3] - f[2]) * (f[3] - f[2]) + (f[3] - f[5]) * (f[3] - f[5])) -
    (f[4] - f[3]) /
    sqrt (EPS + (f[4] - f[3]) * (f[4] - f[3]) + (f[4] - f[6]) * (f[4] - f[6])) -
    (f[1] - f[3]) /
    sqrt (EPS + (f[1] - f[3]) * (f[1] - f[3]) + (f[1] - f[0]) * (f[1] - f[0]));
    write_imagef(out, coord_w, df);
    }
}
