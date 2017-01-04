/*
 * Common functions for this set of filters.
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
#ifndef __UFO_SXC_COMMON_H
#define __UFO_SXC_COMMON_H

#include <ufo/ufo.h>

// Using OpenCL :
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

// This one is copied over from ufo_priv.h, which as implied by its name is private to ufo and hence only accessible when building within ufo-filters.
#ifndef g_list_for
#define g_list_for(list, it) \
        for (it = g_list_first (list); \
             it != NULL; \
             it = g_list_next (it))
#endif

GValue * get_device_info (UfoGpuNode *node, cl_device_info param_name);
gboolean device_has_extension (UfoGpuNode *node, const char * i_ext_name);

#endif
