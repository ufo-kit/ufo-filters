/*
 * Copyright (C) 2011-2015 Karlsruhe Institute of Technology
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

#ifndef UFO_JP2_WRITER_JP2_H
#define UFO_JP2_WRITER_JP2_H

#include <glib-object.h>

G_BEGIN_DECLS

#define UFO_TYPE_JP2_WRITER             (ufo_jp2_writer_get_type())
#define UFO_JP2_WRITER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_JP2_WRITER, UfoJp2Writer))
#define UFO_IS_JP2_WRITER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_JP2_WRITER))
#define UFO_JP2_WRITER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_JP2_WRITER, UfoJp2WriterClass))
#define UFO_IS_JP2_WRITER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_JP2_WRITER))
#define UFO_JP2_WRITER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_JP2_WRITER, UfoJp2WriterClass))


typedef struct _UfoJp2Writer           UfoJp2Writer;
typedef struct _UfoJp2WriterClass      UfoJp2WriterClass;
typedef struct _UfoJp2WriterPrivate    UfoJp2WriterPrivate;

struct _UfoJp2Writer {
    GObject parent_instance;

    UfoJp2WriterPrivate *priv;
};

struct _UfoJp2WriterClass {
    GObjectClass parent_class;
};

UfoJp2Writer  *ufo_jp2_writer_new         (void);
void           ufo_jp2_writer_set_quality (UfoJp2Writer *writer, gint quality);
GType          ufo_jp2_writer_get_type    (void);

G_END_DECLS

#endif
