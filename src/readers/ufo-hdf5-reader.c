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

#include <hdf5.h>
#include "readers/ufo-reader.h"
#include "readers/ufo-hdf5-reader.h"


struct _UfoHdf5ReaderPrivate {
    gchar *dataset;
    hid_t file_id;
    hid_t dataset_id;
    hid_t src_dataspace_id;

    gint n_dims;
    hsize_t dims[3];
    guint current;
};

static void ufo_reader_interface_init (UfoReaderIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoHdf5Reader, ufo_hdf5_reader, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_READER,
                                                ufo_reader_interface_init))

#define UFO_HDF5_READER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_HDF5_READER, UfoHdf5ReaderPrivate))

UfoHdf5Reader *
ufo_hdf5_reader_new (const gchar *dataset)
{
    UfoHdf5Reader *reader = g_object_new (UFO_TYPE_HDF5_READER, NULL);
    reader->priv->dataset = g_strdup (dataset);
    return reader;
}

static void
ufo_hdf5_reader_open (UfoReader *reader,
                      const gchar *filename)
{
    UfoHdf5ReaderPrivate *priv;

    priv = UFO_HDF5_READER_GET_PRIVATE (reader);

    priv->file_id = H5Fopen (filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    priv->dataset_id = H5Dopen (priv->file_id, priv->dataset, H5P_DEFAULT);
    priv->src_dataspace_id = H5Dget_space (priv->dataset_id);
    priv->n_dims = H5Sget_simple_extent_ndims (priv->src_dataspace_id);

    if (priv->n_dims > 3)
        g_error ("read:hdf5: no support for four-dimensional data");

    H5Sget_simple_extent_dims (priv->src_dataspace_id, priv->dims, NULL);

    priv->current = 0;
}

static void
ufo_hdf5_reader_close (UfoReader *reader)
{
    UfoHdf5ReaderPrivate *priv;

    priv = UFO_HDF5_READER_GET_PRIVATE (reader);

    H5Sclose (priv->src_dataspace_id);
    H5Dclose (priv->dataset_id);
    H5Fclose (priv->file_id);
}

static gboolean
ufo_hdf5_reader_data_available (UfoReader *reader)
{
    UfoHdf5ReaderPrivate *priv;

    priv = UFO_HDF5_READER_GET_PRIVATE (reader);

    return priv->current < priv->dims[0];
}

static void
ufo_hdf5_reader_read (UfoReader *reader,
                      UfoBuffer *buffer,
                      UfoRequisition *requisition,
                      guint roi_y,
                      guint roi_height,
                      guint roi_step)
{
    UfoHdf5ReaderPrivate *priv;
    gpointer data;
    hid_t dst_dataspace_id;
    hsize_t dst_dims[2];

    priv = UFO_HDF5_READER_GET_PRIVATE (reader);
    data = ufo_buffer_get_host_array (buffer, NULL);

    hsize_t offset[3] = { priv->current, roi_y, 0 };
    hsize_t count[3] = { 1, roi_height, requisition->dims[0] };

    dst_dims[0] = roi_height;
    dst_dims[1] = requisition->dims[0];
    dst_dataspace_id = H5Screate_simple (2, dst_dims, NULL);

    H5Sselect_hyperslab (priv->src_dataspace_id, H5S_SELECT_SET, offset, NULL, count, NULL);
    H5Dread (priv->dataset_id, H5T_NATIVE_FLOAT, dst_dataspace_id, priv->src_dataspace_id, H5P_DEFAULT, data);
    H5Sclose (dst_dataspace_id);

    priv->current++;
}

static void
ufo_hdf5_reader_get_meta (UfoReader *reader,
                          gsize *width,
                          gsize *height,
                          UfoBufferDepth *bitdepth)
{
    UfoHdf5ReaderPrivate *priv;

    priv = UFO_HDF5_READER_GET_PRIVATE (reader);

    *width = priv->dims[2];
    *height = priv->dims[1];
    *bitdepth = UFO_BUFFER_DEPTH_32F;
}

static void
ufo_hdf5_reader_finalize (GObject *object)
{
    UfoHdf5ReaderPrivate *priv;

    priv = UFO_HDF5_READER_GET_PRIVATE (object);
    g_free (priv->dataset);
    G_OBJECT_CLASS (ufo_hdf5_reader_parent_class)->finalize (object);
}

static void
ufo_reader_interface_init (UfoReaderIface *iface)
{
    iface->open = ufo_hdf5_reader_open;
    iface->close = ufo_hdf5_reader_close;
    iface->read = ufo_hdf5_reader_read;
    iface->get_meta = ufo_hdf5_reader_get_meta;
    iface->data_available = ufo_hdf5_reader_data_available;
}

static void
ufo_hdf5_reader_class_init(UfoHdf5ReaderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = ufo_hdf5_reader_finalize;

    g_type_class_add_private (gobject_class, sizeof (UfoHdf5ReaderPrivate));
}

static void
ufo_hdf5_reader_init (UfoHdf5Reader *self)
{
    UfoHdf5ReaderPrivate *priv = NULL;

    self->priv = priv = UFO_HDF5_READER_GET_PRIVATE (self);
    priv->dataset = NULL;
}
