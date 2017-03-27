#ifndef __ROFEX_H
#define __ROFEX_H

#include <glib.h>

void
make_reordering_schema (guint n_modules,
                        guint n_det_per_module,
                        guint n_fan_proj,
                        guint n_planes,
                        guint n_frames,
                        const gchar *filepath);

void
make_fan2par_params (guint n_modules,
                     guint n_det_per_module,
                     guint n_fan_proj,
                     guint n_planes,
                     guint n_par_proj,
                     guint n_par_dets,
                     gfloat source_offset,
                     const gfloat *source_angle,
                     const gfloat *source_diameter,
                     const gfloat *delta_x,
                     const gfloat *delta_z,
                     gfloat detector_diameter,
                     gfloat image_width,
                     gfloat image_center_x,
                     gfloat image_center_y,
                     const gchar *filepath);

#endif
