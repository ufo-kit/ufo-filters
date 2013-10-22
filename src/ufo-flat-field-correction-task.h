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

#ifndef __UFO_FLAT_FIELD_CORRECTION_TASK_H
#define __UFO_FLAT_FIELD_CORRECTION_TASK_H

#include <ufo/ufo.h>

G_BEGIN_DECLS

#define UFO_TYPE_FLAT_FIELD_CORRECTION_TASK             (ufo_flat_field_correction_task_get_type())
#define UFO_FLAT_FIELD_CORRECTION_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FLAT_FIELD_CORRECTION_TASK, UfoFlatFieldCorrectionTask))
#define UFO_IS_FLAT_FIELD_CORRECTION_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FLAT_FIELD_CORRECTION_TASK))
#define UFO_FLAT_FIELD_CORRECTION_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FLAT_FIELD_CORRECTION_TASK, UfoFlatFieldCorrectionTaskClass))
#define UFO_IS_FLAT_FIELD_CORRECTION_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FLAT_FIELD_CORRECTION_TASK))
#define UFO_FLAT_FIELD_CORRECTION_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FLAT_FIELD_CORRECTION_TASK, UfoFlatFieldCorrectionTaskClass))

typedef struct _UfoFlatFieldCorrectionTask           UfoFlatFieldCorrectionTask;
typedef struct _UfoFlatFieldCorrectionTaskClass      UfoFlatFieldCorrectionTaskClass;
typedef struct _UfoFlatFieldCorrectionTaskPrivate    UfoFlatFieldCorrectionTaskPrivate;

/**
 * UfoFlatFieldCorrectionTask:
 *
 * [ADD DESCRIPTION HERE]. The contents of the #UfoFlatFieldCorrectionTask structure
 * are private and should only be accessed via the provided API.
 */
struct _UfoFlatFieldCorrectionTask {
    /*< private >*/
    UfoTaskNode parent_instance;

    UfoFlatFieldCorrectionTaskPrivate *priv;
};

/**
 * UfoFlatFieldCorrectionTaskClass:
 *
 * #UfoFlatFieldCorrectionTask class
 */
struct _UfoFlatFieldCorrectionTaskClass {
    /*< private >*/
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_flat_field_correction_task_new       (void);
GType     ufo_flat_field_correction_task_get_type  (void);

G_END_DECLS

#endif

