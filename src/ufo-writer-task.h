/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
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

#ifndef __UFO_WRITER_TASK_H
#define __UFO_WRITER_TASK_H

#include <ufo-task-node.h>

G_BEGIN_DECLS

#define UFO_TYPE_WRITER_TASK             (ufo_writer_task_get_type())
#define UFO_WRITER_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_WRITER_TASK, UfoWriterTask))
#define UFO_IS_WRITER_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_WRITER_TASK))
#define UFO_WRITER_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_WRITER_TASK, UfoWriterTaskClass))
#define UFO_IS_WRITER_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_WRITER_TASK))
#define UFO_WRITER_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_WRITER_TASK, UfoWriterTaskClass))

typedef struct _UfoWriterTask           UfoWriterTask;
typedef struct _UfoWriterTaskClass      UfoWriterTaskClass;
typedef struct _UfoWriterTaskPrivate    UfoWriterTaskPrivate;

/**
 * UfoWriterTask:
 *
 * Main object for organizing filters. The contents of the #UfoWriterTask structure
 * are private and should only be accessed via the provided API.
 */
struct _UfoWriterTask {
    /*< private >*/
    UfoTaskNode parent_instance;

    UfoWriterTaskPrivate *priv;
};

/**
 * UfoWriterTaskClass:
 *
 * #UfoWriterTask class
 */
struct _UfoWriterTaskClass {
    /*< private >*/
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_writer_task_new       (void);
GType     ufo_writer_task_get_type  (void);

G_END_DECLS

#endif
