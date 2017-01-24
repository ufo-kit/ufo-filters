/*
 * Gathering statistics on a image stream, copying input to output
 * This file is part of ufo-serge filter set.
 * Copyright (C) 2016 Serge Cohen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Serge Cohen <serge.cohen@synchrotron-soleil.fr>
 */

__kernel void
stat_monitor_f32(__global float *in_img,   // Input image to reduce (will compute min, max, sum, sum of sq per workgroup)
                 __global float *out_stat, // Output : one 4-tupple per workgroup ordered (min, max, sum, sum of sq)
                 __const uint img_size,    // The total number of elements in the image to reduce
                 __local float *local_scr  // A memory scrathpad for within workgroup final reduction
                 )
{
  size_t gi = get_global_id(0);
  float wi_min = + INFINITY; // in_img[gi];
  float wi_max = - INFINITY; // in_img[gi];
  float wi_sum = 0.0f;
  float wi_sum_sq = 0.0f;

  // First part of the reduction : doing it sequentially as much as possible in each work-item :
  while (gi < img_size) {
    float here_val = in_img[gi];
    wi_min = fmin(wi_min, here_val);
    wi_max = fmax(wi_max, here_val);
    wi_sum += here_val;
    wi_sum_sq += here_val * here_val;
    gi += get_global_size(0);
  }

  // Further reduce all the work-items of the current work-group using parallel reduce :
  size_t li = get_local_id(0);
  size_t li_b = li << 2; // there is 4 elements per work-items

  // Initing the parallel reduce
  local_scr[li_b  ] = wi_min;
  local_scr[li_b+1] = wi_max;
  local_scr[li_b+2] = wi_sum;
  local_scr[li_b+3] = wi_sum_sq;
  barrier(CLK_LOCAL_MEM_FENCE);

  // Reducing recursively by a factor of 2 :
  for(size_t offset = get_local_size(0) >> 1; offset != 0; offset >>= 1) {
    if (li < offset) {
      size_t sof = offset<<2;
      local_scr[li_b  ] = fmin(local_scr[li_b  ], local_scr[li_b   + sof]);
      local_scr[li_b+1] = fmax(local_scr[li_b+1], local_scr[li_b+1 + sof]);
      local_scr[li_b+2] += local_scr[li_b+2 + sof];
      local_scr[li_b+3] += local_scr[li_b+3 + sof];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
  }
  if (li == 0) {
    size_t gr_id = get_group_id(0) << 2; // 4 datas per work-group
    out_stat[gr_id  ] = local_scr[0];
    out_stat[gr_id+1] = local_scr[1];
    out_stat[gr_id+2] = local_scr[2];
    out_stat[gr_id+3] = local_scr[3];
  }

}

/***************************************************************/
/* Performing the last round of reduction in workgroup 0 alone */
/***************************************************************/

/*
 * Unfortunately the only way to synchronize all worgroups is to
 * launch another kernel :-(
 */

/* Performing purely parallel reduce based on the workgroup size and number of elements in the
 * input.
 *
 * NB : This kernel is supposed to be run in a single workgroup to fiish up the reduction
 * mostly computed by the above kernel.
 */
__kernel void
stat_monitor_f32_fin(__global float *red_in,
                     __global float *red_out,
                     __const uint num_elts,
                     __local float *local_scr  // A memory scrathpad for within workgroup final reduction
                     )
{
  // Since there is a single workgroup, locol_id and global_id are identical.
  // The total number of workitems is (approximately) half the num_elts
  size_t gi = get_global_id(0);
  size_t grs = get_local_size(0);

  // Copying all data to the local memory, for speed during the reduction
  event_t copied_to_scr = async_work_group_copy(local_scr, red_in, num_elts<<2, 0);
  // Synchronising on the copy being done
  wait_group_events(1, &copied_to_scr);

  size_t curr_n_elts = num_elts;

  // Performing the reduction
  for ( size_t offset = grs; 0 != offset; offset = (offset>>1) + (( 1 != offset ) ? (offset & 0x1L) : 0 ) ) {
    if ( (gi < offset) && (gi+offset < curr_n_elts) ) {
        size_t gi_4B = gi << 2;
        size_t gi_of_4B = (gi + offset) << 2;
        local_scr[gi_4B  ] = fmin(local_scr[gi_4B  ], local_scr[gi_of_4B  ]);
        local_scr[gi_4B+1] = fmax(local_scr[gi_4B+1], local_scr[gi_of_4B+1]);
        local_scr[gi_4B+2] += local_scr[gi_of_4B+2];
        local_scr[gi_4B+3] += local_scr[gi_of_4B+3];
      }
    curr_n_elts = offset;
    barrier(CLK_LOCAL_MEM_FENCE);
  }

  // Returning the results
  event_t copied_from_scr = async_work_group_copy(red_out, local_scr, 4, 0);
  wait_group_events(1, &copied_from_scr);
}



#ifdef cl_khr_fp64
#pragma OPENCL EXTENSION cl_khr_fp64 : enable

__kernel void
stat_monitor_f64(__global float *in_img,   // Input image to reduce (will compute min, max, sum, sum of sq per workgroup)
                 __global double *out_stat, // Output : one 4-tupple per workgroup ordered (min, max, sum, sum of sq)
                 __const uint img_size,    // The total number of elements in the image to reduce
                 __local double *local_scr  // A memory scrathpad for within workgroup final reduction
                 )
{
  size_t gi = get_global_id(0);
  double wi_min = + INFINITY; // convert_double(in_img[gi]);
  double wi_max = - INFINITY; // convert_double(in_img[gi]);
  double wi_sum = 0.0;
  double wi_sum_sq = 0.0;

  // First part of the reduction : doing it sequentially as much as possible in each work-item :
  while (gi < img_size) {
    double here_val = convert_double(in_img[gi]);
    wi_min = fmin(wi_min, here_val);
    wi_max = fmax(wi_max, here_val);
    wi_sum += here_val;
    wi_sum_sq += here_val * here_val;
    gi += get_global_size(0);
  }

  // Further reduce all the work-items of the current work-group using parallel reduce :
  size_t li = get_local_id(0);
  size_t li_b = li << 2; // there is 4 elements per work-items

  // Initing the parallel reduce
  local_scr[li_b  ] = wi_min;
  local_scr[li_b+1] = wi_max;
  local_scr[li_b+2] = wi_sum;
  local_scr[li_b+3] = wi_sum_sq;
  barrier(CLK_LOCAL_MEM_FENCE);

  // Reducing recursively by a factor of 2 :
  for(size_t offset = get_local_size(0) >> 1; offset != 0; offset >>= 1) {
    if (li < offset) {
      size_t sof = offset<<2;
      local_scr[li_b  ] = fmin(local_scr[li_b  ], local_scr[li_b   + sof]);
      local_scr[li_b+1] = fmax(local_scr[li_b+1], local_scr[li_b+1 + sof]);
      local_scr[li_b+2] += local_scr[li_b+2 + sof];
      local_scr[li_b+3] += local_scr[li_b+3 + sof];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
  }
  if (li == 0) {
    size_t gr_id = get_group_id(0) << 2; // 4 datas per work-group
    out_stat[gr_id  ] = local_scr[0];
    out_stat[gr_id+1] = local_scr[1];
    out_stat[gr_id+2] = local_scr[2];
    out_stat[gr_id+3] = local_scr[3];
  }

}


/***************************************************************/
/* Performing the last round of reduction in workgroup 0 alone */
/***************************************************************/

/* Performing purely parallel reduce based on the workgroup size and number of elements in the
 * input.
 *
 * NB : This kernel is supposed to be run in a single workgroup to fiish up the reduction
 * mostly computed by the above kernel.
 */
__kernel void
stat_monitor_f64_fin(__global double *red_in,
                     __global double *red_out,
                     __const uint num_elts,
                     __local double *local_scr  // A memory scrathpad for within workgroup final reduction
                     )
{
  // Since there is a single workgroup, locol_id and global_id are identical.
  // The total number of workitems is (approximately) half the num_elts
  size_t gi = get_global_id(0);
  size_t grs = get_global_size(0); // == get_local_size(0) since a single work-group.

  // Copying all data to the local memory, for speed during the reduction
  event_t copied_to_scr = async_work_group_copy(local_scr, red_in, num_elts<<2, 0);
  // Synchronising on the copy being done
  wait_group_events(1, &copied_to_scr);

  size_t curr_n_elts = num_elts;

  // Performing the reduction
  for ( size_t offset = grs; 0 != offset; offset = (offset>>1) + (( 1 != offset ) ? (offset & 0x1L) : 0 ) ) {
    if ( (gi < offset) && (gi+offset < curr_n_elts) ) {
        size_t gi_4B = gi << 2;
        size_t gi_of_4B = (gi + offset) << 2;
        local_scr[gi_4B  ] = fmin(local_scr[gi_4B  ], local_scr[gi_of_4B  ]);
        local_scr[gi_4B+1] = fmax(local_scr[gi_4B+1], local_scr[gi_of_4B+1]);
        local_scr[gi_4B+2] += local_scr[gi_of_4B+2];
        local_scr[gi_4B+3] += local_scr[gi_of_4B+3];
      }
    curr_n_elts = offset;
    barrier(CLK_LOCAL_MEM_FENCE);
  }

  // Returning the results
  event_t copied_from_scr = async_work_group_copy(red_out, local_scr, 4, 0);
  wait_group_events(1, &copied_from_scr);
}

#endif // cl_khr_fp64
