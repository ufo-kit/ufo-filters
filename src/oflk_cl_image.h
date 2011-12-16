/*
 * oflk_cl_image.h
 *
 *	Image structure storing data declaration.
 *
 *  Created on: Nov 25, 2011
 *      Author: farago
 */

#ifndef OFLK_CL_IMAGE_H_
#define OFLK_CL_IMAGE_H_

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

typedef struct _oflk_cl_image oflk_cl_image;

struct _oflk_cl_image {
    cl_mem image_mem;
    unsigned int width;
    unsigned int height;
    cl_image_format image_format;
};

/*
 * Release allocated resources.
 * \param *image pointer to oflk_cl_image
 *
 * \return CL_SUCCESS if everything went OK, error number otherwise
 */
cl_int oflk_cl_image_release(oflk_cl_image *image);

#endif /* OFLK_CL_IMAGE_H_ */
