/*
 * Copyright (C) 2011-2017 Karlsruhe Institute of Technology
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

#define MODE_SUM(a, b) ((a) + (b))
#define MODE_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MODE_MAX(a, b) (((a) > (b)) ? (a) : (b))

#define DEFINE_PARALLEL_KERNEL(operation)                                                    \
kernel void reduce_##operation (global float *input,                                         \
                                global float *output,                                        \
                                local float *cache,                                          \
                                const ulong real_size,                                       \
                                const int pixels_per_thread)                                 \
{                                                                                            \
    int lid = get_local_id (0);                                                              \
    size_t gid = get_global_id (0);                                                          \
    size_t global_size = get_global_size (0);                                                \
    float value = 0.0f;                                                                      \
                                                                                             \
    for (int i = 0; i < pixels_per_thread; i++) {                                            \
        if (gid < real_size) {                                                               \
            value = operation(value, input[gid]);                                            \
        }                                                                                    \
        gid += global_size;                                                                  \
    }                                                                                        \
                                                                                             \
    cache[lid] = value;                                                                      \
    barrier (CLK_LOCAL_MEM_FENCE);                                                           \
                                                                                             \
    for (int block = get_local_size (0) >> 1; block > 0; block >>= 1) {                      \
        if (lid < block) {                                                                   \
            cache[lid] = operation(cache[lid], cache[lid + block]);                          \
        }                                                                                    \
        barrier (CLK_LOCAL_MEM_FENCE);                                                       \
    }                                                                                        \
                                                                                             \
    if (lid == 0) {                                                                          \
        output[get_group_id (0)] = cache[0];                                                 \
    }                                                                                        \
}                                                                                            \

DEFINE_PARALLEL_KERNEL(MODE_SUM)
DEFINE_PARALLEL_KERNEL(MODE_MIN)
DEFINE_PARALLEL_KERNEL(MODE_MAX)

kernel void parallel_sum_2D (global float *input,  
                             global float *output,
                             local float *cache,
                             const int offset,
                             const int width,
                             const int roi_width,
                             const int roi_height)
{
    int lid = get_local_id (0);
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int global_size_y = get_global_size (1);
    int block;
    float tmp = 0.0f;

    /* Load more pixels per work item to keep the GPU busy. */
    while (idy < roi_height) {
        tmp += idx < roi_width ? input[idy * width + idx + offset] : 0.0f;
        idy += global_size_y;
    }
    cache[lid] = tmp;
    barrier (CLK_LOCAL_MEM_FENCE);

    /* Parallel sum, every work item adds its own cached value plus the one's a
     * block further with block being halved in every iteration. */
    for (block = get_local_size (0) >> 1; block > 0; block >>= 1) {
        if (lid < block) {
            cache[lid] += cache[lid + block];
        }
        barrier (CLK_LOCAL_MEM_FENCE);
    }

    /* First work item in a work group stores the computed value, thus we have
     * to sum the computed values of each work group on host. */
    if (lid == 0) {
        output[get_group_id (1) * get_num_groups (0) + get_group_id (0)] = cache[0];
    }
}
