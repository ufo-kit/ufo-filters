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

#include "ufo-sxc-common.h"

// Required headers :
#include <string.h>


GValue *
get_device_info (UfoGpuNode *node, cl_device_info param_name)
{
  cl_command_queue cmd_queue;
  cl_device_id dev_cl;

  cmd_queue = ufo_gpu_node_get_cmd_queue (node);
  UFO_RESOURCES_CHECK_CLERR(clGetCommandQueueInfo(cmd_queue, CL_QUEUE_DEVICE, sizeof(cl_device_id), &dev_cl, NULL));

  cl_uint uint_val;
  cl_ulong ulong_val;
  cl_bool bool_val;
  size_t sizet_val;
  size_t sizet_x3_val[3];
  char string_val[2048];

  // Getting ready to output the value/GValue
  GValue *value;
  value = g_new0 (GValue, 1);
  memset (value, 0, sizeof (GValue));

  switch (param_name) {
    // All the next ones are cl_uint, so sharing the code :
  case CL_DEVICE_VENDOR_ID:
  case CL_DEVICE_MAX_COMPUTE_UNITS:
  case CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS:
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR:
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT:
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT:
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG:
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT:
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE:
  case CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF:
  case CL_DEVICE_MAX_CLOCK_FREQUENCY:
  case CL_DEVICE_ADDRESS_BITS:
  case CL_DEVICE_MAX_READ_IMAGE_ARGS:
  case CL_DEVICE_MAX_WRITE_IMAGE_ARGS:
  case CL_DEVICE_MAX_SAMPLERS:
  case CL_DEVICE_MEM_BASE_ADDR_ALIGN:
  case CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE:
  case CL_DEVICE_MAX_CONSTANT_ARGS:
  case CL_DEVICE_PARTITION_MAX_SUB_DEVICES:
  case CL_DEVICE_REFERENCE_COUNT:
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, param_name, sizeof(cl_uint), &uint_val, NULL));
    g_value_init (value, G_TYPE_UINT);
    g_value_set_uint (value, uint_val);
    break;
    //All the next ones are cl_ulong, so sharing the code :
  case CL_DEVICE_MAX_MEM_ALLOC_SIZE:
  case CL_DEVICE_GLOBAL_MEM_CACHE_SIZE:
  case CL_DEVICE_GLOBAL_MEM_SIZE:
  case CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE:
  case CL_DEVICE_LOCAL_MEM_SIZE:
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, param_name, sizeof(cl_ulong), &ulong_val, NULL));
    g_value_init (value, G_TYPE_ULONG);
    g_value_set_ulong (value, ulong_val);
    break;
    //All the next ones are cl_bool, so sharing the code :
  case CL_DEVICE_IMAGE_SUPPORT:
  case CL_DEVICE_ERROR_CORRECTION_SUPPORT:
  case CL_DEVICE_HOST_UNIFIED_MEMORY:
  case CL_DEVICE_ENDIAN_LITTLE:
  case CL_DEVICE_AVAILABLE:
  case CL_DEVICE_COMPILER_AVAILABLE:
  case CL_DEVICE_LINKER_AVAILABLE:
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, param_name, sizeof(cl_bool), &bool_val, NULL));
    g_value_init (value, G_TYPE_BOOLEAN);
    g_value_set_boolean (value, bool_val);
    break;
    //All the next ones are size_t, so sharing the code :
  case CL_DEVICE_MAX_WORK_GROUP_SIZE:
  case CL_DEVICE_IMAGE2D_MAX_WIDTH:
  case CL_DEVICE_IMAGE2D_MAX_HEIGHT:
  case CL_DEVICE_IMAGE3D_MAX_WIDTH:
  case CL_DEVICE_IMAGE3D_MAX_HEIGHT:
  case CL_DEVICE_IMAGE3D_MAX_DEPTH:
  case CL_DEVICE_IMAGE_MAX_BUFFER_SIZE:
  case CL_DEVICE_IMAGE_MAX_ARRAY_SIZE:
  case CL_DEVICE_MAX_PARAMETER_SIZE:
  case CL_DEVICE_PROFILING_TIMER_RESOLUTION:
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, param_name, sizeof(size_t), &sizet_val, NULL));
    g_value_init (value, G_TYPE_ULONG);
    g_value_set_ulong (value, sizet_val);
    break;
    //All the next ones are size_t[], so sharing the code :
  case CL_DEVICE_MAX_WORK_ITEM_SIZES:
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, param_name, sizeof(sizet_x3_val), sizet_x3_val, NULL));
    g_free(value);
    value = g_new0 (GValue, 3);
    memset (value, 0, 3*sizeof (GValue));
    g_value_init (value, G_TYPE_ULONG);
    g_value_set_ulong (value, sizet_x3_val[0]);
    g_value_init (value+1, G_TYPE_ULONG);
    g_value_set_ulong (value+1, sizet_x3_val[1]);
    g_value_init (value+2, G_TYPE_ULONG);
    g_value_set_ulong (value+2, sizet_x3_val[2]);
    break;
    //All the next ones are char[], so sharing the code :
  case CL_DEVICE_BUILT_IN_KERNELS:
  case CL_DEVICE_NAME:
  case CL_DEVICE_VENDOR:
  case CL_DRIVER_VERSION:
  case CL_DEVICE_PROFILE:
  case CL_DEVICE_VERSION:
  case CL_DEVICE_OPENCL_C_VERSION:
  case CL_DEVICE_EXTENSIONS:
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, param_name, sizeof(string_val), string_val, NULL));
    g_value_init (value, G_TYPE_STRING);
    g_value_set_string (value, string_val);
    break;
  }
  return value;
}

gboolean
device_has_extension (UfoGpuNode *node, const char * i_ext_name)
{
  cl_command_queue cmd_queue;
  cl_device_id dev_cl;

  cmd_queue = ufo_gpu_node_get_cmd_queue (node);
  UFO_RESOURCES_CHECK_CLERR(clGetCommandQueueInfo(cmd_queue, CL_QUEUE_DEVICE, sizeof(cl_device_id), &dev_cl, NULL));

  char exts_val[2048];
  char *ret_strstr = NULL;

  UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, CL_DEVICE_EXTENSIONS, sizeof(exts_val), exts_val, NULL));
  ret_strstr = strstr(exts_val, i_ext_name);
  return(NULL != ret_strstr);
}

