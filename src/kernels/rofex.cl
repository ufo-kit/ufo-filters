/*
 * Copyright (C) 2013-2017 Karlsruhe Institute of Technology
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

 kernel void
 compute_attenuation (global float *sino_in,
                      global float *sino_out,
                      global float *avg_ref,
                      global float *avg_dark,
                      const float temp,
                      const unsigned int n_dets,
                      const unsigned int n_proj,
                      const unsigned int planeInd)
{
    const int x = get_global_id(0);
    const int y = get_global_id(1);

    if ( x >= n_dets || y >= n_proj)
        return;

    int sinoInd = x + n_dets * y;
    float numerator = (float) (sino_in[sinoInd])
          - avg_dark[x + planeInd * n_dets];

    float denominator = avg_ref[planeInd * n_dets * n_proj + sinoInd]
          - avg_dark[planeInd * n_dets + x];

    if (numerator < temp)
      numerator = temp;
    if (denominator < temp)
      denominator = temp;

    sino_out[sinoInd] = -log(numerator / denominator);
}

kernel
void mask_sino (global float *sino_in,
                global float *mask,
                global float *sino_out,
                const unsigned int n_dets,
                const unsigned int n_proj)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if ( x >= n_dets || y >= n_proj)
      return;

  int sinoInd = x + n_dets * y;
  sino_out[sinoInd] = sino_in[sinoInd] * mask[sinoInd];
}
