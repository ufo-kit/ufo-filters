/*
 * Replacing pixels by local median value when detected as outliers based on MAD
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
#ifndef __UFO_MED_MAD_REJECT_TASK_H
#define __UFO_MED_MAD_REJECT_TASK_H

#include <ufo/ufo.h>

G_BEGIN_DECLS

#define UFO_TYPE_MED_MAD_REJECT_TASK             (ufo_med_mad_reject_task_get_type())
#define UFO_MED_MAD_REJECT_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_MED_MAD_REJECT_TASK, UfoMedMadRejectTask))
#define UFO_IS_MED_MAD_REJECT_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_MED_MAD_REJECT_TASK))
#define UFO_MED_MAD_REJECT_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_MED_MAD_REJECT_TASK, UfoMedMadRejectTaskClass))
#define UFO_IS_MED_MAD_REJECT_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_MED_MAD_REJECT_TASK))
#define UFO_MED_MAD_REJECT_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_MED_MAD_REJECT_TASK, UfoMedMadRejectTaskClass))

typedef struct _UfoMedMadRejectTask           UfoMedMadRejectTask;
typedef struct _UfoMedMadRejectTaskClass      UfoMedMadRejectTaskClass;
typedef struct _UfoMedMadRejectTaskPrivate    UfoMedMadRejectTaskPrivate;

struct _UfoMedMadRejectTask {
    UfoTaskNode parent_instance;

    UfoMedMadRejectTaskPrivate *priv;
};

struct _UfoMedMadRejectTaskClass {
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_med_mad_reject_task_new       (void);
GType     ufo_med_mad_reject_task_get_type  (void);

G_END_DECLS

#endif
