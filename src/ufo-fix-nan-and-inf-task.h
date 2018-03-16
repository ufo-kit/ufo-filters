/*
 * Copyright (C) 2011-2017 Karlsruhe Institute of Technology
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

#ifndef __UFO_FIX_NAN_AND_INF_TASK_H
#define __UFO_FIX_NAN_AND_INF_TASK_H

#include <ufo/ufo.h>

G_BEGIN_DECLS

#define UFO_TYPE_FIX_NAN_AND_INF_TASK             (ufo_fix_nan_and_inf_task_get_type())
#define UFO_FIX_NAN_AND_INF_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FIX_NAN_AND_INF_TASK, UfoFixNanAndInfTask))
#define UFO_IS_FIX_NAN_AND_INF_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FIX_NAN_AND_INF_TASK))
#define UFO_FIX_NAN_AND_INF_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FIX_NAN_AND_INF_TASK, UfoFixNanAndInfTaskClass))
#define UFO_IS_FIX_NAN_AND_INF_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FIX_NAN_AND_INF_TASK))
#define UFO_FIX_NAN_AND_INF_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FIX_NAN_AND_INF_TASK, UfoFixNanAndInfTaskClass))

typedef struct _UfoFixNanAndInfTask           UfoFixNanAndInfTask;
typedef struct _UfoFixNanAndInfTaskClass      UfoFixNanAndInfTaskClass;
typedef struct _UfoFixNanAndInfTaskPrivate    UfoFixNanAndInfTaskPrivate;

struct _UfoFixNanAndInfTask {
    UfoTaskNode parent_instance;

    UfoFixNanAndInfTaskPrivate *priv;
};

struct _UfoFixNanAndInfTaskClass {
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_fix_nan_and_inf_task_new       (void);
GType     ufo_fix_nan_and_inf_task_get_type  (void);

G_END_DECLS

#endif
