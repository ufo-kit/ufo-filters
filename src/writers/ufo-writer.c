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

#include "ufo-writer.h"

typedef UfoWriterIface UfoWriterInterface;

G_DEFINE_INTERFACE (UfoWriter, ufo_writer, 0)


void
ufo_writer_open (UfoWriter *writer,
                 const gchar *filename)
{
    UFO_WRITER_GET_IFACE (writer)->open (writer, filename);
}

void
ufo_writer_close (UfoWriter *writer)
{
    UFO_WRITER_GET_IFACE (writer)->close (writer);
}

void
ufo_writer_write (UfoWriter *writer,
                  gpointer data,
                  UfoRequisition *requisition,
                  UfoBufferDepth depth)
{
    UFO_WRITER_GET_IFACE (writer)->write (writer, data, requisition, depth);
}

static void
ufo_writer_default_init (UfoWriterInterface *iface)
{
}
