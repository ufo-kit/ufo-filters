
const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_LINEAR; 

__kernel void forwardproject(
        __read_only image2d_t slice,
        __global float *sinogram, 
        float angle_step)
{
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int slice_width = get_global_size(0);

    const float angle = idy * angle_step;
    /* radius of object circle */
    const float r = slice_width / 2.0f; 
    /* positive/negative distance from detector center */
    const float d = idx - r; 
    /* length of the cut through the circle */
    const float l = sqrt(4.0f*r*r - 4.0f*d*d);

    /* vector in detector direction */
    float2 D = (float2) (cos(angle), sin(angle)); 
    D = normalize(D);

    /* vector perpendicular to the detector */
    const float2 N = (float2) (D.y, -D.x); 

    /* sample point in the circle traveling along N */
    float2 sample = d * D - l/2.0f * N + ((float2) (r, r));
    float sum = 0.0f;

    for (int i = 0; i < l; i++) {
        sum += read_imagef(slice, sampler, sample).x;
        sample += N;
    }

    sinogram[idy * slice_width + idx] = sum;
}
