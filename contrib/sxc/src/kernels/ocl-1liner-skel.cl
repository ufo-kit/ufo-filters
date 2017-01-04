/*
 * Instant compiling a one liner OpenCL filter
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

/*
 * In this kernel, the one line imported can use multiple ways to address the
 * modified pixel.
 * In general the model is that there is exactly one work-item per pixel and
 * the work is performed on a 2D grid matching exactly the image diemensions.
 *
 * in0 .. inN are input 1D array(s).
 * out is 1D output array.
 *
 * Those can be addressed using px_index which is the /current/ pixel for the workitem.
 *
 * One can also access a /random/ pixel value using the macro : IMG_VAL which takes three
 * arguments : the (x, y) pixel coordinate, and the array pointer for the image.
 * NB : this macro acn be used also for lvalue (but it is risky that a work-item
 * modifies a pixel value in =out= that is NOT the one indexed by px_index.
 */

#define IMG_VAL(hx,hy,image) image[(hx) + (hy)*sizeX]

__kernel void
ocl_1liner (
            %s // The inputs...
            __global float *out,
            )
{
  size_t sizeX = get_global_size(0);
  size_t sizeY = get_global_size(1);
  size_t x = get_global_id(0);
  size_t y = get_global_id(1);

  size_t px_index = x + y*sizeX;

  // And here the one line of /variable/ code :
  %s;
}
