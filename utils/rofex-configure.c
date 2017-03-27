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

#include <ufo/ufo.h>
#include <stdio.h>
#include "rofex.h"

int main (int argc, char *argv[])
{
    /* TODO: provide options or configuration file
    */

    guint n_modules = 27;
    guint n_det_per_module = 16;
    guint n_fan_proj = 500;
    guint n_planes = 2;
    guint n_frames = 500;
    gchar *schema_path = "/home/ashkarin/Suren/ufo2/tests/reordering_schema.raw";

    make_reordering_schema (n_modules,
                            n_det_per_module,
                            n_fan_proj,
                            n_planes,
                            n_frames,
                            schema_path);

/*
    guint n_par_proj = 512;
    guint n_par_dets = 256;
    gfloat source_offset = 23.2;
    gfloat detector_diameter = 216.0;
    gfloat image_width = 190.0;
    gfloat image_center_x = 0.0;
    gfloat image_center_y = 0.0;

    gfloat source_angle[2] = { 240.0, 240.0 };
    gfloat source_diameter[2] = { 365.0, 370.0 };
    gfloat delta_x[2] = { 815.0, 815.0 };
    gfloat delta_z[2] = { 1417.0, 1430.0 };

    gchar *params_path = "/home/ashkarin/Suren/ufo2/tests/params-512.raw";
*/
    guint n_par_proj = 512;
    guint n_par_dets = 512;
    gfloat source_offset = 23.2;
    gfloat detector_diameter = 900.0;
    gfloat image_width = 512.0;
    gfloat image_center_x = 0.0;
    gfloat image_center_y = 0.0;

    gfloat source_angle[2] = { 240.0, 240.0 };
    gfloat source_diameter[2] = { 365.0, 370.0 };
    gfloat delta_x[2] = { 815.0, 815.0 };
    gfloat delta_z[2] = { 1417.0, 1430.0 };

    gchar *params_path = "/home/ashkarin/Suren/ufo2/tests/params-512-2.raw";
    make_fan2par_params (n_modules,
                         n_det_per_module,
                         n_fan_proj,
                         n_planes,
                         n_par_proj,
                         n_par_dets,
                         source_offset,
                         source_angle,
                         source_diameter,
                         delta_x,
                         delta_z,
                         detector_diameter,
                         image_width,
                         image_center_x,
                         image_center_y,
                         params_path);
}
