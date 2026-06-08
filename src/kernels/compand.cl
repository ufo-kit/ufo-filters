/*
 * Copyright (C) 2026 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#define COMPAND_PI_F 3.14159265358979323846f
#define COMPAND_HALF_PI_F 1.57079632679489661923f
#define COMPAND_ONE_LIMIT 0.9999999403953552f
#define COMPAND_HALF_PI_LIMIT 1.5707962512969971f

inline int
compand_idx (void)
{
    return get_global_id (1) * get_global_size (0) + get_global_id (0);
}

inline float
compand_scale_factor (float delta, float dynamic_range)
{
    return 2.0f / (delta * dynamic_range);
}

inline float
compand_arctan_scale_factor (float delta, float dynamic_range)
{
    return COMPAND_PI_F / (delta * dynamic_range);
}

inline float
compand_quantize (float value)
{
    return rint (value);
}

inline float
compand_compress (float scaled, float dynamic_range)
{
    return compand_quantize ((1.0f + scaled) * 0.5f * dynamic_range);
}

inline float
compand_expand_input (float value, float dynamic_range)
{
    return 2.0f * value / dynamic_range - 1.0f;
}

kernel void
tanh_compand_forward (global float *in,
                      global float *out,
                      float center,
                      float delta,
                      float dynamic_range)
{
    int idx = compand_idx ();
    float scale_factor = compand_scale_factor (delta, dynamic_range);
    float scaled = tanh ((in[idx] - center) * scale_factor);

    out[idx] = compand_compress (scaled, dynamic_range);
}

kernel void
tanh_compand_backward (global float *in,
                       global float *out,
                       float center,
                       float delta,
                       float dynamic_range)
{
    int idx = compand_idx ();
    float scale_factor = compand_scale_factor (delta, dynamic_range);
    float scaled = clamp (compand_expand_input (in[idx], dynamic_range),
                          -COMPAND_ONE_LIMIT,
                          COMPAND_ONE_LIMIT);

    out[idx] = atanh (scaled) / scale_factor + center;
}

kernel void
arctan_compand_forward (global float *in,
                        global float *out,
                        float center,
                        float delta,
                        float dynamic_range)
{
    int idx = compand_idx ();
    float scale_factor = compand_arctan_scale_factor (delta, dynamic_range);
    float scaled = 2.0f / COMPAND_PI_F * atan ((in[idx] - center) * scale_factor);

    out[idx] = compand_compress (scaled, dynamic_range);
}

kernel void
arctan_compand_backward (global float *in,
                         global float *out,
                         float center,
                         float delta,
                         float dynamic_range)
{
    int idx = compand_idx ();
    float scale_factor = compand_arctan_scale_factor (delta, dynamic_range);
    float scaled = clamp (compand_expand_input (in[idx], dynamic_range) * COMPAND_HALF_PI_F,
                          -COMPAND_HALF_PI_LIMIT,
                          COMPAND_HALF_PI_LIMIT);

    out[idx] = tan (scaled) / scale_factor + center;
}

kernel void
recip_sqrt_compand_forward (global float *in,
                            global float *out,
                            float center,
                            float delta,
                            float dynamic_range)
{
    int idx = compand_idx ();
    float scale_factor = compand_scale_factor (delta, dynamic_range);
    float scaled = (in[idx] - center) * scale_factor;

    out[idx] = compand_compress (scaled / sqrt (1.0f + scaled * scaled), dynamic_range);
}

kernel void
recip_sqrt_compand_backward (global float *in,
                             global float *out,
                             float center,
                             float delta,
                             float dynamic_range)
{
    int idx = compand_idx ();
    float scale_factor = compand_scale_factor (delta, dynamic_range);
    float scaled = clamp (compand_expand_input (in[idx], dynamic_range),
                          -COMPAND_ONE_LIMIT,
                          COMPAND_ONE_LIMIT);

    out[idx] = scaled / sqrt (1.0f - scaled * scaled) / scale_factor + center;
}

kernel void
clip_compand_forward (global float *in,
                      global float *out,
                      float center,
                      float delta,
                      float dynamic_range)
{
    int idx = compand_idx ();
    float scale_factor = compand_scale_factor (delta, dynamic_range);
    float scaled = clamp ((in[idx] - center) * scale_factor, -1.0f, 1.0f);

    out[idx] = compand_compress (scaled, dynamic_range);
}

kernel void
clip_compand_backward (global float *in,
                       global float *out,
                       float center,
                       float delta,
                       float dynamic_range)
{
    int idx = compand_idx ();
    float scale_factor = compand_scale_factor (delta, dynamic_range);
    float scaled = clamp (compand_expand_input (in[idx], dynamic_range),
                          -COMPAND_ONE_LIMIT,
                          COMPAND_ONE_LIMIT);

    out[idx] = scaled / scale_factor + center;
}
