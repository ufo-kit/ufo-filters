/*
 * Replacing pixels by local median value when detected as outliers based on MAD (2D box, variable size)
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

// The size of the box in which to perform the filtering
// This one should be provided at compile time through a -D directive.
// #define BOXSIZE 15

// The threshold to apply on the MAD to reject (or not) the pixel's value

typedef int ssize_t;

constant const ssize_t half_box = (BOXSIZE-1)>>1;

__kernel void
med_mad_rej_2D (
                __global float *iImage,
                __global float *oFilteredImage,
                const  float iRejThresh
                )
{
  size_t sizeX = get_global_size(0);
  size_t sizeY = get_global_size(1);
  size_t globalX = get_global_id(0);
  size_t globalY = get_global_id(1);

  // Getting the element of an image (both lvalue and rvalue) from index :
#define FromImage(x,y,image) image[(x) + (y)*sizeX]

  /* Getting all the value within the box and in the image : */
  float v[BOXSIZE*BOXSIZE];

  ssize_t cX, cY;
  size_t index=0;
  for ( cX=-half_box + globalX; (half_box + globalX + 1) != cX; ++cX) {
    for ( cY=-half_box + globalY; (half_box + globalY + 1) != cY; ++cY) {
      if ( (0 <= cX) && (sizeX > cX) && (0 <= cY) && (sizeY > cY) ) {
        v[index] = FromImage(cX, cY, iImage);
        ++index;
      }
    }
  }

  size_t num_px = index;
  float swapper;

  /* Computing the median : */
  for (index = 0; num_px != (1+index); ++index) {
    for (size_t j = 0; j!= (num_px-index); ++j ) {
      swapper = v[j];
      v[j] = min(v[j], v[j+1]);
      v[j+1] = max(swapper, v[j+1]);
    }
  }
  float med = v[num_px>>1];

  /* Computing the MAD */
  for ( index = 0; (num_px>>1) != index; ++index) {
    v[index] = med - v[index];
  }
  for ( index = (num_px>>1) ; num_px != index; ++index) {
    v[index] = v[index] - med;
  }
  for (index = 0; num_px != (1+index); ++index) {
    for (size_t j = 0; j!= (num_px-index); ++j ) {
      swapper = v[j];
      v[j] = min(v[j], v[j+1]);
      v[j+1] = max(swapper, v[j+1]);
    }
  }
  float mad = v[num_px>>1];

  /* Testing the current value : */
  if ( fabs(FromImage(globalX, globalY, iImage) - med) < (mad*iRejThresh) )
    FromImage(globalX, globalY, oFilteredImage) = FromImage(globalX, globalY, iImage);
  else
    FromImage(globalX, globalY, oFilteredImage) = med;
}
