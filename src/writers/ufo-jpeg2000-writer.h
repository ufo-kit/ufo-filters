/*
 * Copyright (C) 2026 Karlsruhe Institute of Technology
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

#ifndef UFO_JPEG2000_WRITER_H
#define UFO_JPEG2000_WRITER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define UFO_TYPE_JPEG2000_WRITER             (ufo_jpeg2000_writer_get_type())
#define UFO_JPEG2000_WRITER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_JPEG2000_WRITER, UfoJpeg2000Writer))
#define UFO_IS_JPEG2000_WRITER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_JPEG2000_WRITER))
#define UFO_JPEG2000_WRITER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_JPEG2000_WRITER, UfoJpeg2000WriterClass))
#define UFO_IS_JPEG2000_WRITER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_JPEG2000_WRITER))
#define UFO_JPEG2000_WRITER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_JPEG2000_WRITER, UfoJpeg2000WriterClass))

typedef struct _UfoJpeg2000Writer           UfoJpeg2000Writer;
typedef struct _UfoJpeg2000WriterClass      UfoJpeg2000WriterClass;
typedef struct _UfoJpeg2000WriterPrivate    UfoJpeg2000WriterPrivate;

struct _UfoJpeg2000Writer {
    GObject parent_instance;

    UfoJpeg2000WriterPrivate *priv;
};

struct _UfoJpeg2000WriterClass {
    GObjectClass parent_class;
};

UfoJpeg2000Writer *ufo_jpeg2000_writer_new      (void);
GType              ufo_jpeg2000_writer_get_type (void);

G_END_DECLS

#endif
