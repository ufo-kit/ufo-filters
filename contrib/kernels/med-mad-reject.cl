/*
 * Replacing pixels by local median value when detected as outliers based on MAD (3x3x3 box)
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


/* Kernel to reject ouliers based on distance to median larger than ... times mad. */
__kernel void
outliersRej_MedMad_3x3x3_f32(
                             __global float *iPreviousImage,
                             __global float *iThisImage,
                             __global float *iNextImage,
                             __global float *oFilteredImage,
                             const  float iRejThresh
                             )
{
  bool Zfront = (0 == iPreviousImage);
  bool Zrear = (0 == iNextImage);

  size_t ioStride = get_global_size(0);
  size_t globalX = get_global_id(0);
  size_t globalY = get_global_id(1);

  // Pixel in image buffers : globalX + globalY*ioStride
  // Pixel in work item : localX, localY
  // Pixel in __local buffer : [globalX][YinBuf]

  // Gathering pixel values from the locally centered box :
  // Using the floowing macro to simplify lisibility
#define FromImage(x,y,image) image[(x) + (y)*ioStride]

  // Performing first the gathering from the current frame (we are sure it is existing)
  // so that we can copy the corresponding values for previous or next frames when those
  // are not provided.

  float v[27];

  bool X_m1 = ( globalX ), X_p1 = ( (globalX + 1) != ioStride );
  bool Y_m1 = ( globalY ), Y_p1 = ( (globalY + 1) != get_global_size(1) );

  // Indexes 0-8 correspond to Zfront
  // Indexes 9-17 to current
  // Indexes 18-26 to Zrear

  /* X horizontal (left to right), Y vertical (bottom-up), we have for current frame
   * (-9 for Zfront and +9 for Zrear ):
   *
   *             |-----------+------------+-----------|
   *   globalY+1 | 15        | 16         | 17        |
   *   globalY   | 12        | 13         | 14        |
   *   globalY-1 |  9        | 10         | 11        |
   *             |-----------+------------+-----------|
   *             | globalX-1 | globalX    | globalX+1 |
   */

  v[13] = FromImage(globalX  , globalY  , iThisImage);
  v[10] = (Y_m1) ? FromImage(globalX  , globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
  v[16] = (Y_p1) ? FromImage(globalX  , globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
  if ( X_m1 ) {
    v[12] = FromImage(globalX-1, globalY  , iThisImage);
    v[9]  = (Y_m1) ? FromImage(globalX-1, globalY-1, iThisImage) : FromImage(globalX-1, globalY, iThisImage);
    v[15] = (Y_p1) ? FromImage(globalX-1, globalY+1, iThisImage) : FromImage(globalX-1, globalY, iThisImage);
  }
  else {
    v[12] = FromImage(globalX, globalY  , iThisImage);
    v[9]  = (Y_m1) ? FromImage(globalX, globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    v[15] = (Y_p1) ? FromImage(globalX, globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
  }
  if ( X_p1 ) {
    v[14] = FromImage(globalX+1, globalY  , iThisImage);
    v[11]  = (Y_m1) ? FromImage(globalX+1, globalY-1, iThisImage) : FromImage(globalX+1, globalY, iThisImage);
    v[17] = (Y_p1) ? FromImage(globalX+1, globalY+1, iThisImage) : FromImage(globalX+1, globalY, iThisImage);
  }
  else {
    v[14] = FromImage(globalX, globalY  , iThisImage);
    v[11] = (Y_m1) ? FromImage(globalX, globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    v[17] = (Y_p1) ? FromImage(globalX, globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
  }

  if ( Zfront ) { // Missing front frame; values copied from current frame.
    v[0] = v[9];
    v[1] = v[10];
    v[2] = v[11];
    v[3] = v[12];
    v[4] = v[13];
    v[5] = v[14];
    v[6] = v[15];
    v[7] = v[16];
    v[8] = v[17];
  }
  else {
    v[ 4] = FromImage(globalX  , globalY  , iThisImage);
    v[ 1] = (Y_m1) ? FromImage(globalX  , globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    v[ 7] = (Y_p1) ? FromImage(globalX  , globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    if ( X_m1 ) {
      v[ 3] = FromImage(globalX-1, globalY  , iThisImage);
      v[ 0] = (Y_m1) ? FromImage(globalX-1, globalY-1, iThisImage) : FromImage(globalX-1, globalY, iThisImage);
      v[ 6] = (Y_p1) ? FromImage(globalX-1, globalY+1, iThisImage) : FromImage(globalX-1, globalY, iThisImage);
    }
    else {
      v[ 3] = FromImage(globalX, globalY  , iThisImage);
      v[ 0] = (Y_m1) ? FromImage(globalX, globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
      v[ 6] = (Y_p1) ? FromImage(globalX, globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    }
    if ( X_p1 ) {
      v[ 5] = FromImage(globalX+1, globalY  , iThisImage);
      v[ 2] = (Y_m1) ? FromImage(globalX+1, globalY-1, iThisImage) : FromImage(globalX+1, globalY, iThisImage);
      v[ 8] = (Y_p1) ? FromImage(globalX+1, globalY+1, iThisImage) : FromImage(globalX+1, globalY, iThisImage);
    }
    else {
      v[ 5] = FromImage(globalX, globalY  , iThisImage);
      v[ 2] = (Y_m1) ? FromImage(globalX, globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
      v[ 8] = (Y_p1) ? FromImage(globalX, globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    }
  }

  if ( Zrear ) { // Missing rear frame; values copied from current frame.
    v[18] = v[9];
    v[19] = v[10];
    v[20] = v[11];
    v[21] = v[12];
    v[22] = v[13];
    v[23] = v[14];
    v[24] = v[15];
    v[25] = v[16];
    v[26] = v[17];
  }
  else {
    v[22] = FromImage(globalX  , globalY  , iThisImage);
    v[19] = (Y_m1) ? FromImage(globalX  , globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    v[25] = (Y_p1) ? FromImage(globalX  , globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    if ( X_m1 ) {
      v[21] = FromImage(globalX-1, globalY  , iThisImage);
      v[18] = (Y_m1) ? FromImage(globalX-1, globalY-1, iThisImage) : FromImage(globalX-1, globalY, iThisImage);
      v[24] = (Y_p1) ? FromImage(globalX-1, globalY+1, iThisImage) : FromImage(globalX-1, globalY, iThisImage);
    }
    else {
      v[21] = FromImage(globalX, globalY  , iThisImage);
      v[18] = (Y_m1) ? FromImage(globalX, globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
      v[24] = (Y_p1) ? FromImage(globalX, globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    }
    if ( X_p1 ) {
      v[23] = FromImage(globalX+1, globalY  , iThisImage);
      v[20] = (Y_m1) ? FromImage(globalX+1, globalY-1, iThisImage) : FromImage(globalX+1, globalY, iThisImage);
      v[26] = (Y_p1) ? FromImage(globalX+1, globalY+1, iThisImage) : FromImage(globalX+1, globalY, iThisImage);
    }
    else {
      v[23] = FromImage(globalX, globalY  , iThisImage);
      v[20] = (Y_m1) ? FromImage(globalX, globalY-1, iThisImage) : FromImage(globalX, globalY, iThisImage);
      v[26] = (Y_p1) ? FromImage(globalX, globalY+1, iThisImage) : FromImage(globalX, globalY, iThisImage);
    }
  }

  // We can finally start the computation itself :
#undef b
#undef bub
#undef rabbit_up
#undef rabbit_down
#define b(a, b) {float swap=a; a=min(a, b); b=max(swap, b);}
#define bub(a) b(v[a], v[a+1])
#define rabbit_up bub(0) bub(1) bub(2) bub(3) bub(4) bub(5) bub(6) bub(7) bub(8) bub(9) bub(10) bub(11) bub(12) bub(13)
#define rabbit_down bub(25) bub(24) bub(23) bub(22) bub(21) bub(20) bub(19) bub(18) bub(17) bub(16) bub(15) bub(14) bub(13) bub(12)

  rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down

    float median = v[13];
  // Now computing the MAD :
  /*
#undef abs_diff_med
#define abs_diff_med(a) v[a] = fabs(v[a] - median);
  abs_diff_med(0)
    abs_diff_med(1)
    abs_diff_med(2)
    abs_diff_med(3)
    abs_diff_med(4)
    abs_diff_med(5)
    abs_diff_med(6)
    abs_diff_med(7)
    abs_diff_med(8)
    abs_diff_med(9)
    abs_diff_med(10)
    abs_diff_med(11)
    abs_diff_med(12)
    abs_diff_med(13)
    abs_diff_med(14)
    abs_diff_med(15)
    abs_diff_med(16)
    abs_diff_med(17)
    abs_diff_med(18)
    abs_diff_med(19)
    abs_diff_med(20)
    abs_diff_med(21)
    abs_diff_med(22)
    abs_diff_med(23)
    abs_diff_med(24)
    abs_diff_med(25)
    abs_diff_med(26)
  */
  // Since the array is ordered we now that all index < 13 are lower than median
  // and index > 13 are greater than median (we can make the economy of the fabs)
  v[0]  = median - v[0];
  v[1]  = median - v[1];
  v[2]  = median - v[2];
  v[3]  = median - v[3];
  v[4]  = median - v[4];
  v[5]  = median - v[5];
  v[6]  = median - v[6];
  v[7]  = median - v[7];
  v[8]  = median - v[8];
  v[9]  = median - v[9];
  v[10] = median - v[10];
  v[11] = median - v[11];
  v[12] = median - v[12];
  v[13] = 0.0f;
  v[14] -= median;
  v[15] -= median;
  v[16] -= median;
  v[17] -= median;
  v[18] -= median;
  v[19] -= median;
  v[20] -= median;
  v[21] -= median;
  v[22] -= median;
  v[23] -= median;
  v[24] -= median;
  v[25] -= median;
  v[26] -= median;

  // And the median of that again :
  rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down
    rabbit_up
    rabbit_down

    float mad = v[13];

  // If the distance to the median is larger than iRejThresh*mad, then replace pixel's value by the median :

  if ( fabs(FromImage(globalX  , globalY  , iThisImage) - median) < (iRejThresh*mad) )
    oFilteredImage[globalX + globalY*ioStride] = FromImage(globalX  , globalY  , iThisImage);
  else
    oFilteredImage[globalX + globalY*ioStride] = median;

  //  oFilteredImage[globalX + globalY*ioStride] = mad;

}
