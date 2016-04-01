#ifndef LAMINO_ROI_H
#define LAMINO_ROI_H

#define EXTRACT_INT(region, index) g_value_get_int (g_value_array_get_nth ((region), (index)))

#include <glib-object.h>

G_BEGIN_DECLS

void clip (gint result[2], gfloat extrema[2], gint maximum);
void determine_x_extrema (gfloat extrema[2], GValueArray *x_extrema, GValueArray *y_extrema,
                          gfloat tomo_angle, gfloat x_center);
void determine_y_extrema (gfloat extrema[2], GValueArray *x_extrema, GValueArray *y_extrema,
                          gfloat z_extrema[2], gfloat tomo_angle, gfloat lamino_angle,
                          gfloat y_center);
void determine_x_region (gint result[2], GValueArray *x_extrema, GValueArray *y_extrema, gfloat tomo_angle,
                         gfloat x_center, gint width);
void determine_y_region (gint result[2], GValueArray *x_extrema, GValueArray *y_extrema, gfloat z_extrema[2],
                         gfloat tomo_angle, gfloat lamino_angle, gfloat y_center, gint height);

G_END_DECLS

#endif
