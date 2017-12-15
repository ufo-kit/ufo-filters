/*
 * Gathering statistics on a image stream, copying input to output
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
#ifndef __UFO_STAT_MONITOR_TASK_H
#define __UFO_STAT_MONITOR_TASK_H

#include <ufo/ufo.h>

G_BEGIN_DECLS

#define UFO_TYPE_STAT_MONITOR_TASK             (ufo_stat_monitor_task_get_type())
#define UFO_STAT_MONITOR_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_STAT_MONITOR_TASK, UfoStatMonitorTask))
#define UFO_IS_STAT_MONITOR_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_STAT_MONITOR_TASK))
#define UFO_STAT_MONITOR_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_STAT_MONITOR_TASK, UfoStatMonitorTaskClass))
#define UFO_IS_STAT_MONITOR_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_STAT_MONITOR_TASK))
#define UFO_STAT_MONITOR_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_STAT_MONITOR_TASK, UfoStatMonitorTaskClass))

typedef struct _UfoStatMonitorTask           UfoStatMonitorTask;
typedef struct _UfoStatMonitorTaskClass      UfoStatMonitorTaskClass;
typedef struct _UfoStatMonitorTaskPrivate    UfoStatMonitorTaskPrivate;

struct _UfoStatMonitorTask {
  UfoTaskNode parent_instance;

  UfoStatMonitorTaskPrivate *priv;
};

struct _UfoStatMonitorTaskClass {
  UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_stat_monitor_task_new       (void);
GType     ufo_stat_monitor_task_get_type  (void);

G_END_DECLS

#endif // __UFO_STAT_MONITOR_TASK_H
