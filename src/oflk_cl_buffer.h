/*
 * oflk_cl_buffer.h
 *
 *	Buffer structure storing data declaration.
 *
 *  Created on: Nov 25, 2011
 *      Author: farago
 */

#ifndef OFLK_CL_BUFFER_H_
#define OFLK_CL_BUFFER_H_

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

typedef struct _oflk_cl_buffer oflk_cl_buffer;

struct _oflk_cl_buffer {
	cl_mem mem;
	unsigned int width;
	unsigned int height;
	cl_image_format image_format; /* the type of image data this temp buffer reflects */
};

/*
 * Release allocated resources.
 * \param *image pointer to oflk_cl_image
 *
 * \return CL_SUCCESS if everything went OK, error number otherwise
 */
cl_int oflk_cl_buffer_release(oflk_cl_buffer *buffer);

#endif /* OFLK_CL_BUFFER_H_ */
