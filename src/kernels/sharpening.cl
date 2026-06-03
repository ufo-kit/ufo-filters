/*
 * Copyright (C) 2026
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

#define COMMON_DIGITAL_FREQUENCY_SETUP                                \
    const int width = get_global_size (0);                            \
    const int height = get_global_size (1);                           \
    const int real_width = width >> 1;                                \
    const int idx = get_global_id (0);                                \
    const int idy = get_global_id (1);                                \
    const int index = idy * width + idx;                              \
    const int fidx = idx >> 1;                                        \
    float freq_x = (fidx >= real_width >> 1) ? fidx - real_width : fidx; \
    float freq_y = (idy >= height >> 1) ? idy - height : idy;         \
    freq_x = freq_x / real_width;                                     \
    freq_y = freq_y / height;

inline float
limit_boost (const float raw,
             const float max_boost)
{
    return max_boost > 0.0f ? max_boost * tanh (raw / max_boost) : raw;
}

kernel void
frequency_sharpen_laplace (global float *input,
                           global float *output,
                           const float strength,
                           const float max_boost)
{
    COMMON_DIGITAL_FREQUENCY_SETUP;
    const float raw = 4.0f * M_PI_F * M_PI_F * strength * (freq_x * freq_x + freq_y * freq_y);
    const float factor = 1.0f + limit_boost (raw, max_boost);

    output[index] = input[index] * factor;
}

kernel void
frequency_sharpen_discrete_laplace (global float *input,
                                    global float *output,
                                    const float strength,
                                    const float max_boost)
{
    COMMON_DIGITAL_FREQUENCY_SETUP;
    const float sx = sin (M_PI_F * freq_x);
    const float sy = sin (M_PI_F * freq_y);
    const float raw = 4.0f * strength * (sx * sx + sy * sy);
    const float factor = 1.0f + limit_boost (raw, max_boost);

    output[index] = input[index] * factor;
}

kernel void
frequency_sharpen_lorentz (global float *input,
                           global float *output,
                           const float strength,
                           const float max_boost,
                           const float fwhm)
{
    COMMON_DIGITAL_FREQUENCY_SETUP;
    const float radius = sqrt (freq_x * freq_x + freq_y * freq_y);
    const float raw = strength * (exp (M_PI_F * radius * fwhm) - 1.0f);
    const float factor = 1.0f + limit_boost (raw, max_boost);

    output[index] = input[index] * factor;
}
