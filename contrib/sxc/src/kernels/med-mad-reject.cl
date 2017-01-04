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

  //  __local float groupBox[get_local_size(0)+2][get_local_size(1)+2][3];
  // Conversion using the following RegExp (Perl style) : groupBox\[([-[:alnum:] .+*]*)\]\[([-[:alnum:] .+*]*)\]\[([-[:alnum:] .+*]*)\] => GB(\1, \2, \3)
  //#define GB(a,b,c) groupBox[a + lSizeX * (b + lSizeY * c)]
#define FromImage(x,y,image) image[(x) + (y)*ioStride]

  // We can finally start the computation itself :
  float v[27];

  bool X_m1 = ( globalX ), X_p1 = ( (globalX + 1) != ioStride );
  bool Y_m1 = ( globalY ), Y_p1 = ( (globalY + 1) != get_global_size(1) );

  if ( Zfront ) {
    v[0] = v[1] = v[2] = v[3] = v[4] = v[5] = v[6] = v[7] = v[8] = 0.0f;
  }
  else {
    v[0]  = ( X_m1 && Y_m1 ) ? FromImage(globalX-1, globalY-1, iPreviousImage) : 0.0f;
    v[1]  = ( X_m1 )         ? FromImage(globalX-1, globalY  , iPreviousImage) : 0.0f;
    v[2]  = ( X_m1 && Y_p1 ) ? FromImage(globalX-1, globalY+1, iPreviousImage) : 0.0f;

    v[3]  =         ( Y_m1 ) ? FromImage(globalX  , globalY-1, iPreviousImage) : 0.0f;
    v[4]  =                    FromImage(globalX  , globalY  , iPreviousImage);
    v[5]  =         ( Y_p1 ) ? FromImage(globalX  , globalY+1, iPreviousImage) : 0.0f;

    v[6]  = ( X_p1 && Y_m1 ) ? FromImage(globalX+1, globalY-1, iPreviousImage) : 0.0f;
    v[7]  = ( X_p1 )         ? FromImage(globalX+1, globalY  , iPreviousImage) : 0.0f;
    v[8]  = ( X_p1 && Y_p1 ) ? FromImage(globalX+1, globalY+1, iPreviousImage) : 0.0f;
  }

  v[9]  = ( X_m1 && Y_m1 ) ? FromImage(globalX-1, globalY-1, iThisImage) : 0.0f;
  v[10] = ( X_m1 )         ? FromImage(globalX-1, globalY  , iThisImage) : 0.0f;
  v[11] = ( X_m1 && Y_p1 ) ? FromImage(globalX-1, globalY+1, iThisImage) : 0.0f;

  v[12] =         ( Y_m1 ) ? FromImage(globalX  , globalY-1, iThisImage) : 0.0f;
  v[13] =                    FromImage(globalX  , globalY  , iThisImage);
  v[14] =         ( Y_p1 ) ? FromImage(globalX  , globalY+1, iThisImage) : 0.0f;

  v[15] = ( X_p1 && Y_m1 ) ? FromImage(globalX+1, globalY-1, iThisImage) : 0.0f;
  v[16] = ( X_p1 )         ? FromImage(globalX+1, globalY  , iThisImage) : 0.0f;
  v[17] = ( X_p1 && Y_p1 ) ? FromImage(globalX+1, globalY+1, iThisImage) : 0.0f;

  if ( Zrear ) {
    v[18] = v[19] = v[20] = v[21] = v[22] = v[23] = v[24] = v[25] = v[26] = 0.0f;
  }
  else {
    v[18] = ( X_m1 && Y_m1 ) ? FromImage(globalX-1, globalY-1, iNextImage) : 0.0f;
    v[19] = ( X_m1 )         ? FromImage(globalX-1, globalY  , iNextImage) : 0.0f;
    v[20] = ( X_m1 && Y_p1 ) ? FromImage(globalX-1, globalY+1, iNextImage) : 0.0f;

    v[21] =         ( Y_m1 ) ? FromImage(globalX  , globalY-1, iNextImage) : 0.0f;
    v[22] =                    FromImage(globalX  , globalY  , iNextImage);
    v[23] =         ( Y_p1 ) ? FromImage(globalX  , globalY+1, iNextImage) : 0.0f;

    v[24] = ( X_p1 && Y_m1 ) ? FromImage(globalX+1, globalY-1, iNextImage) : 0.0f;
    v[25] = ( X_p1 )         ? FromImage(globalX+1, globalY  , iNextImage) : 0.0f;
    v[26] = ( X_p1 && Y_p1 ) ? FromImage(globalX+1, globalY+1, iNextImage) : 0.0f;
  }

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
