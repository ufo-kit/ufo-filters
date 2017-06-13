kernel void
fill_zeros(global float *data,
           const unsigned int width,
           const unsigned int height,
           const unsigned int depth)
{
    const unsigned long x = get_global_id(0);
    const unsigned long y = get_global_id(1);
    const unsigned long z = get_global_id(2);

    if (x >= width || y >= height || z >= depth) {
        return;
    }

    const unsigned long idx = x + y * width + z * (width * height);
    data[idx] = 0;
}

kernel void
amplif (global const float *input,
        global float *output,
        const unsigned int n_vals,
        const unsigned int n_trans_per_portion,
        const unsigned int n_modpairs,
        const ushort amp_bit15,
        const ushort amp_bit16)
{
    const unsigned long val_id = get_global_id(0);
    const unsigned long trans_local = get_global_id(1);
    const unsigned long modpair = get_global_id(2);

    if (val_id >= n_vals ||
        trans_local >= n_trans_per_portion ||
        modpair >= n_modpairs)
    {
        return;
    }

    unsigned long index;
    ushort val, bit15, bit16;
    bool is_amp_bit15, is_amp_bit16;

    index = val_id + trans_local * n_vals + modpair * (n_vals * n_trans_per_portion);

    val = input[index];
    bit15 = 0x01 << 14;
    bit16 = 0x01 << 15;

    is_amp_bit15 = val & bit15;
    is_amp_bit16 = val & bit16;

    // Get the value using only 14 bits
    val ^= bit15;
    val ^= bit16;

    // Correct the value
    val += is_amp_bit15 ? amp_bit15 : 0;
    val += is_amp_bit16 ? amp_bit16 : 0;

    // Set the value
    output[index] = val;
}


kernel void
reorder(global const float *input,
        global float *output,
        const unsigned int portion,
        const unsigned int n_trans_per_portion,
        const unsigned int n_fan_dets,
        const unsigned int n_fan_proj,
        // ROFEX parameters
        const unsigned int n_rings,
        const unsigned int n_dets_per_module,
        const unsigned int n_mods_per_ring,
        global const unsigned int *beam_positions,
        const unsigned int n_beam_positions,
        global const int *rings_selection_mask,
        const unsigned int rings_selection_mask_size,
        // Precomputed
        global const unsigned int *dets_map)
{
    const unsigned long det = get_global_id(0);
    const unsigned long proj = get_global_id(1);
    const unsigned long trans_local = get_global_id(2);
    const unsigned int n_modpairs_per_ring = n_mods_per_ring / 2;

    if (det >= n_fan_dets ||
        proj >= n_fan_proj ||
        trans_local >= n_trans_per_portion)
    {
        return;
    }

    long trans_global, beam_position, ring;
    unsigned int modpair, idx_sino_val, idx_sino, idx_in, idx_out;
    unsigned int offset_per_modpair, offset_per_ring, offset_per_trans;
    unsigned int modpair_offset, proj_offset, ring_offset, trans_local_offset;
    unsigned int sino_offset;

    // Compute the ring which is hitted by the beam at current transition.
    trans_global = portion * n_trans_per_portion + trans_local;
    beam_position = beam_positions[trans_global % n_beam_positions];

    // Compute the pair of modules that correspond to the current detector
    // pixel at the given transition.
    modpair = (det / n_dets_per_module) % n_modpairs_per_ring;

    // Compute the index of the value in a sinogram.
    idx_sino_val = det + proj * n_fan_dets;

    // Compute offsets in the input data per modpair, ring and transition.
    offset_per_modpair = n_dets_per_module * n_fan_proj * n_trans_per_portion;
    offset_per_ring = n_modpairs_per_ring * offset_per_modpair;
    offset_per_trans = n_dets_per_module * n_fan_proj;

    // Compute offsets independent on generated sinogram.
    modpair_offset = modpair * offset_per_modpair;
    proj_offset = proj * n_dets_per_module;

    // Go through the rings in the neighborhood and create sinograms for
    // each of those rings using the data measured at the specific ring at
    // this transition.
    for (unsigned int i = 0; i < rings_selection_mask_size; ++i) {
        ring = beam_position + rings_selection_mask[i];
        if (ring < 0 || ring >= n_rings) {
            continue;
        }

        ring_offset = ring * offset_per_ring;
        trans_local_offset = trans_local * offset_per_trans;

        idx_in = ring_offset
                 + modpair_offset
                 + trans_local_offset
                 + proj_offset
                 + (dets_map[idx_sino_val] - 1);

        // Output data is organized as data chunks describing each transition
        idx_sino = trans_local * rings_selection_mask_size + i;
        sino_offset = idx_sino * (n_fan_dets * n_fan_proj);

        idx_out = sino_offset + idx_sino_val;
        if (dets_map[idx_sino_val]) {
            output[idx_out] = input[idx_in];
        }
    }
}

kernel void
attenuation(global const float *input,
            global float *output,
            const unsigned int portion,
            const unsigned int n_trans_per_portion,
            const unsigned int n_fan_dets,
            const unsigned int n_fan_proj,
            // ROFEX parameters
            const unsigned int n_rings,
            global const int *beam_positions,
            const unsigned int n_beam_positions,
            global const int *rings_selection_mask,
            const unsigned int rings_selection_mask_size,
            // Precomputed
            global const float *avg_flats,
            global const float *avg_darks)
{
    const float eps = 1E-5;
    const unsigned long det = get_global_id(0);
    const unsigned long proj = get_global_id(1);
    const unsigned long trans_local = get_global_id(2);

    if (det >= n_fan_dets ||
        proj >= n_fan_proj ||
        trans_local >= n_trans_per_portion)
    {
        return;
    }

    long trans_global, beam_position, ring;
    float numerator, denominator;
    unsigned long idx_sino_val, idx_flats, idx_darks, idx_sino, idx_data;

    // Compute the ring which is hitted by the beam at current transition.
    trans_global = portion * n_trans_per_portion + trans_local;
    beam_position = beam_positions[trans_global % n_beam_positions];

    // Compute the index of the value in a sinogram.
    idx_sino_val = det + proj * n_fan_dets;

    for (unsigned int i = 0; i < rings_selection_mask_size; ++i) {
        ring = beam_position + rings_selection_mask[i];
        if (ring < 0 || ring >= n_rings) {
            continue;
        }

        idx_flats = idx_sino_val + ring * (n_fan_dets * n_fan_proj);
        idx_darks = det + ring * n_fan_dets;

        idx_sino = trans_local * rings_selection_mask_size + i;
        idx_data  = idx_sino_val + idx_sino * (n_fan_dets * n_fan_proj);

        numerator = input[idx_data] - avg_darks[idx_darks];
        denominator = avg_flats[idx_flats] - avg_darks[idx_darks];

        numerator = (numerator < eps) ? eps : numerator;
        denominator = (denominator < eps) ? eps : denominator;

        output[idx_data] = -log (numerator / denominator);
    }
}


// Fan 2 Par
float
interp_ray (unsigned long index,
            // Input
            global const float *sino_fan,
            unsigned int n_fan_dets,
            unsigned long sino_fan_offset,
            // parameters
            global const float *gamma,
            global const float *gamma_before,
            global const float *gamma_after,
            global const float *gamma_goal,
            global const float *theta,
            global const float *theta_before,
            global const float *theta_after,
            global const float *theta_goal)
{
    unsigned long idx_a, idx_b;
    float factor;
    float w1, w2, w3, w4;
    float v1, v2;
    float tb, ta, tg, gb, ga, gg;

    tb = theta_before[index];
    ta = theta_after[index];
    tg = theta_goal[index];

    gb = gamma_before[index];
    ga = gamma_after[index];
    gg = gamma_goal[index];

    idx_a = gb + tb * n_fan_dets + sino_fan_offset;
    w1 = sino_fan[idx_a];

    idx_a = ga + tb * n_fan_dets + sino_fan_offset;
    w2 = sino_fan[idx_a];

    idx_a = gb + ta * n_fan_dets + sino_fan_offset;
    w3 = sino_fan[idx_a];

    idx_a = ga + ta * n_fan_dets + sino_fan_offset;
    w4 = sino_fan[idx_a];

    idx_a = (unsigned long) tb;
    idx_b = (unsigned long) ta;

    factor = tg - theta[idx_a];
    factor = factor / (theta[idx_b] - theta[idx_a]);
    v1 = w1 + factor * (w3 - w1);
    v2 = w2 + factor * (w4 - w2);

    idx_a = (unsigned long) gb;
    idx_b = (unsigned long) ga;
    factor = gg - gamma[idx_a];
    factor = factor / (gamma[idx_b] - gamma[idx_a]);

    return v1 + factor * (v2 - v1);
}

float
comp_val (unsigned long index,
          // Input
          global const float *sino_fan,
          unsigned int  n_fan_dets,
          unsigned long sino_fan_offset,
          // Params
          global const float **ray,
          global const float *gamma,
          global const float **gamma_before,
          global const float **gamma_after,
          global const float **gamma_goal,
          global const float *theta,
          global const float **theta_before,
          global const float **theta_after,
          global const float **theta_goal)
{
    float v, factor, res;
    factor = 1.0 / (float) (ray[0][index] + ray[1][index]);
    res = 0;

    for (int i = 0; i < 2; i++) {
        // Type conversion to avoid false positive cases.
        if ((int)ray[i][index]) {
            v = interp_ray(index, sino_fan, n_fan_dets, sino_fan_offset,
                        gamma, gamma_before[i], gamma_after[i], gamma_goal[i],
                        theta, theta_before[i], theta_after[i], theta_goal[i]);

            res += ray[i][index] * v * factor;
        }
    }

    return res * 0.5;
}

kernel
void fan2par_interp (global const float *sino_fan,
                     global float *sino_par,
                     const unsigned int portion,
                     const unsigned int trans_per_portion,
                     const unsigned int n_fan_dets,
                     const unsigned int n_fan_proj,
                     const unsigned int n_par_dets,
                     const unsigned int n_par_proj,
                     // ROFEX parameters
                     const float detector_r,
                     const unsigned int n_rings,
                     global const int *beam_positions,
                     const unsigned int n_beam_positions,
                     global const int *rings_selection_mask,
                     const unsigned int rings_selection_mask_size,
                     // Precomputed
                     global const float *params,
                     const unsigned int param_offset)
{
    const unsigned long det = get_global_id(0);
    const unsigned long proj = get_global_id(1);
    const unsigned long trans_local = get_global_id(2);

    if (det >= n_par_dets ||
        proj >= n_par_proj ||
        trans_local >= trans_per_portion)
    {
        return;
    }

    // Parameters
    global const float *theta = params + 0;
    global const float *gamma = theta + param_offset;
    global const float *s = gamma + param_offset;
    global const float *alpha_circle = s + param_offset;

    global const float *theta_after[2] =
      { alpha_circle + param_offset, alpha_circle + 2 * param_offset };

    global const float *theta_before[2] =
      { alpha_circle + 3 * param_offset, alpha_circle + 4 * param_offset };

    global const float *theta_goal[2] =
      { alpha_circle + 5 * param_offset, alpha_circle + 6 * param_offset };

    global const float *gamma_after[2] =
      { alpha_circle + 7 * param_offset, alpha_circle + 8 * param_offset };

    global const float *gamma_before[2] =
      { alpha_circle + 9 * param_offset, alpha_circle + 10 * param_offset };

    global const float *gamma_goal[2] =
      { alpha_circle + 11 * param_offset, alpha_circle + 12 * param_offset };

    global const float *ray[2] =
      { alpha_circle + 13 * param_offset, alpha_circle + 14 * param_offset };

    //
    float factor1, factor2, res;
    long trans_global, beam_position, ring;
    unsigned long det2, proj2;
    unsigned long idx_sino, idx_out, index;
    unsigned long sino_fan_offset, sino_par_offset, ring_par_offset;

    // Compute
    unsigned long half_par_dets;

    half_par_dets = n_par_dets / 2;
    proj2 = n_par_proj + proj;
    det2 = (det < half_par_dets) ? n_par_dets - det - 1 :
                                   half_par_dets - (det % half_par_dets) - 1;

    factor1 = s[det] / detector_r;
    factor2 = s[det2] / detector_r;


    // Compute the ring which is hitted by the beam at current transition.
    trans_global = portion * trans_per_portion + trans_local;
    beam_position = beam_positions[trans_global % n_beam_positions];

    // Compute the index of the value in a sinogram.
    for (unsigned int i = 0; i < rings_selection_mask_size; ++i) {
        ring = beam_position + rings_selection_mask[i];
        if (ring < 0 || ring >= n_rings) {
            continue;
        }

        res = 0;
        idx_sino = trans_local * rings_selection_mask_size + i;
        sino_fan_offset = idx_sino * (n_fan_dets * n_fan_proj);
        sino_par_offset = idx_sino * (n_par_proj * n_par_dets);
        ring_par_offset = ring * (n_par_proj * n_par_dets);

        idx_out = det + proj * n_par_dets + sino_par_offset;

        // Part one
        if (factor1 >= - 1 && factor1 <= 1) {
            index = det + proj * n_par_dets + ring_par_offset;
            res += comp_val(index, sino_fan, n_fan_dets, sino_fan_offset, ray,
                            gamma, gamma_after, gamma_before, gamma_goal,
                            theta, theta_after, theta_before, theta_goal);
        }

        // Second part
        if (factor2 >= - 1 && factor2 <= 1) {
            index = det2 + proj2 * n_par_dets + ring_par_offset;
            res += comp_val(index, sino_fan, n_fan_dets, sino_fan_offset, ray,
                            gamma, gamma_after, gamma_before, gamma_goal,
                            theta, theta_after, theta_before, theta_goal);
        }

        sino_par[idx_out] = res;
    }
}
