/*
 * oflk_cl_buffer.c
 *
 *	Buffer structure storing data implementation.
 *
 *  Created on: Nov 30, 2011
 *      Author: farago
 */
#include "CL/cl.h"

#include "oflk_cl_buffer.h"

cl_int oflk_cl_buffer_release(oflk_cl_buffer *buffer) {
	return clReleaseMemObject(buffer->mem);
}
