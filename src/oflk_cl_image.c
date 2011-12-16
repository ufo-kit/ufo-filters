/*
 * oflk_cl_image.c
 *
 *	Image structure storing data implementation.
 *
 *  Created on: Nov 30, 2011
 *      Author: farago
 */
#include <CL/cl.h>

#include "oflk_cl_image.h"

cl_int oflk_cl_image_release(oflk_cl_image *image) {
	return clReleaseMemObject(image->image_mem);
}
