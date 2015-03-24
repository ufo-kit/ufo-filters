#ifndef UFO_HDF5_H
#define UFO_HDF5_H

#undef H5_USE_16_API

#define H5Dopen_vers    2
#define H5Dcreate_vers  2
#define H5Gopen_vers    2
#define H5Gcreate_vers  2

#include <glib.h>
#include <hdf5.h>

gboolean ufo_hdf5_can_open (const gchar *filename);

#endif
