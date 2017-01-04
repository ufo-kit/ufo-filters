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


#ifndef __UFO_OCL_1LINER_TASK_H
#define __UFO_OCL_1LINER_TASK_H

#include <ufo/ufo.h>

G_BEGIN_DECLS

#define UFO_TYPE_OCL_1LINER_TASK             (ufo_ocl_1liner_task_get_type())
#define UFO_OCL_1LINER_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_OCL_1LINER_TASK, UfoOCL1LinerTask))
#define UFO_IS_OCL_1LINER_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_OCL_1LINER_TASK))
#define UFO_OCL_1LINER_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_OCL_1LINER_TASK, UfoOCL1LinerTaskClass))
#define UFO_IS_OCL_1LINER_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_OCL_1LINER_TASK))
#define UFO_OCL_1LINER_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_OCL_1LINER_TASK, UfoOCL1LinerTaskClass))

typedef struct _UfoOCL1LinerTask           UfoOCL1LinerTask;
typedef struct _UfoOCL1LinerTaskClass      UfoOCL1LinerTaskClass;
typedef struct _UfoOCL1LinerTaskPrivate    UfoOCL1LinerTaskPrivate;

struct _UfoOCL1LinerTask {
  UfoTaskNode parent_instance;

  UfoOCL1LinerTaskPrivate *priv;
};

struct _UfoOCL1LinerTaskClass {
  UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_ocl_1liner_task_new       (void);
GType     ufo_ocl_1liner_task_get_type  (void);

G_END_DECLS

#endif
