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

#ifndef __UFO_ROFEX_DUMMY_DETECTOR_TASK_H
#define __UFO_ROFEX_DUMMY_DETECTOR_TASK_H

#include <ufo/ufo.h>

G_BEGIN_DECLS

#define UFO_TYPE_ROFEX_DUMMY_DETECTOR_TASK             (ufo_rofex_dummy_detector_task_get_type())
#define UFO_ROFEX_DUMMY_DETECTOR_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_ROFEX_DUMMY_DETECTOR_TASK, UfoRofexDummyDetectorTask))
#define UFO_IS_ROFEX_DUMMY_DETECTOR_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_ROFEX_DUMMY_DETECTOR_TASK))
#define UFO_ROFEX_DUMMY_DETECTOR_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_ROFEX_DUMMY_DETECTOR_TASK, UfoRofexDummyDetectorTaskClass))
#define UFO_IS_ROFEX_DUMMY_DETECTOR_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_ROFEX_DUMMY_DETECTOR_TASK))
#define UFO_ROFEX_DUMMY_DETECTOR_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_ROFEX_DUMMY_DETECTOR_TASK, UfoRofexDummyDetectorTaskClass))

typedef struct _UfoRofexDummyDetectorTask           UfoRofexDummyDetectorTask;
typedef struct _UfoRofexDummyDetectorTaskClass      UfoRofexDummyDetectorTaskClass;
typedef struct _UfoRofexDummyDetectorTaskPrivate    UfoRofexDummyDetectorTaskPrivate;

struct _UfoRofexDummyDetectorTask {
    UfoTaskNode parent_instance;

    UfoRofexDummyDetectorTaskPrivate *priv;
};

struct _UfoRofexDummyDetectorTaskClass {
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_rofex_dummy_detector_task_new       (void);
GType     ufo_rofex_dummy_detector_task_get_type  (void);

G_END_DECLS

#endif
