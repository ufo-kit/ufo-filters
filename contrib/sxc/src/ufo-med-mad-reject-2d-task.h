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

#ifndef __UFO_MED_MAD_REJECT_2D_TASK_H
#define __UFO_MED_MAD_REJECT_2D_TASK_H

#include <ufo/ufo.h>

G_BEGIN_DECLS

#define UFO_TYPE_MED_MAD_REJECT_2D_TASK             (ufo_med_mad_reject_2d_task_get_type())
#define UFO_MED_MAD_REJECT_2D_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_MED_MAD_REJECT_2D_TASK, UfoMedMadReject2DTask))
#define UFO_IS_MED_MAD_REJECT_2D_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_MED_MAD_REJECT_2D_TASK))
#define UFO_MED_MAD_REJECT_2D_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_MED_MAD_REJECT_2D_TASK, UfoMedMadReject2DTaskClass))
#define UFO_IS_MED_MAD_REJECT_2D_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_MED_MAD_REJECT_2D_TASK))
#define UFO_MED_MAD_REJECT_2D_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_MED_MAD_REJECT_2D_TASK, UfoMedMadReject2DTaskClass))

typedef struct _UfoMedMadReject2DTask           UfoMedMadReject2DTask;
typedef struct _UfoMedMadReject2DTaskClass      UfoMedMadReject2DTaskClass;
typedef struct _UfoMedMadReject2DTaskPrivate    UfoMedMadReject2DTaskPrivate;

struct _UfoMedMadReject2DTask {
    UfoTaskNode parent_instance;

    UfoMedMadReject2DTaskPrivate *priv;
};

struct _UfoMedMadReject2DTaskClass {
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_med_mad_reject_2d_task_new       (void);
GType     ufo_med_mad_reject_2d_task_get_type  (void);

G_END_DECLS

#endif
