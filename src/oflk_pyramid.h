/*
 * oflk_pyramid.h
 *
 * Structure storing different levels of image pyramids.
 *
 *  Created on: Nov 25, 2011
 *      Author: farago
 */

#ifndef OFLK_PYRAMID_H_
#define OFLK_PYRAMID_H_

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "oflk_cl_buffer.h"
#include "oflk_cl_image.h"

#define OFLK_INVALID_PYRAMID_TYPE -1;

typedef struct _oflk_pyramid *oflk_pyramid_p; /** pointer to the pyramid structure */

/**
 * olfk_pyramid_p:
 *
 * Optical flow pyramid.
 */
struct _oflk_pyramid {
	unsigned int levels; /** number of image pyramid levels */
	cl_image_format image_format;
	oflk_cl_image *image_levels; /** array of oflk_cl_image structures holding data for different levels */
    oflk_cl_buffer scratch_buf; /** tmp buffer */
    oflk_cl_image scratch_img; /** tmp image */
    cl_context context; /** opencl context */
    cl_command_queue command_queue; /** opencl command queue */
    cl_event event; /** opencl event for timing information */
};

/**
 * oflk_pyramid_init:
 * @levels: number of pyramid levels
 * @channel_order: channel order
 * @channel_type: channel_type
 * @context: cl_context
 * @command_queue: cl_command_queue
 * @width: width of an image at the 0th level (not undersampled)
 * @height: height of an image at the 0th level (not undersampled)
 * @err_num: pointer to cl_int error number
 *
 * Initialize pyramid.
 *
 * Returns: pointer to oflk_pyramid
 */
oflk_pyramid_p oflk_pyramid_init(unsigned int levels,
						cl_channel_order channel_order,
						cl_channel_type channel_type,
						cl_context context,
						cl_command_queue command_queue,
						unsigned int width,
						unsigned int height,
						cl_int *err_num);

/**
 * oflk_pyramid_release:
 * @pyramid_p: pointer to a pointer to the pyramid to be released. Double
 * 			pointer enables us to make the actual pointer to the pyramid
 * 			to point to a NULL structure.
 *
 * Release specified pyramid.
 *
 * Returns: 0 on success, opencl error code caused by releasing objects otherwise
 */
cl_int oflk_pyramid_release(oflk_pyramid_p *pyramid_p);

/**
 * oflk_pyramid_fill:
 * @pyramid_p: pointer to a pyramid
 * @oflk_image: oflk_cl_image structure holidng the data
 * @downfilter_x: cl_kernel subsampler in x direction
 * @downfilter_y: cl_kernel subsampler in y direction
 *
 * Fill a pyramid with subsampled levels.
 *
 * Returns: opencl error code
 */
cl_int oflk_pyramid_fill(oflk_pyramid_p pyramid_p,
						oflk_cl_image *oflk_image,
						cl_kernel downfilter_x,
						cl_kernel downfilter_y);

/**
 * oflk_pyramid_fill_derivative:
 * @pyramid_p: pyramid which will be filled with derivatves
 * @other_pyramid_p: pyramid from which the derivatives will be calculated
 * @kernel_x: cl_kernel kernel derivating in x-direction
 * @kernel_y: cl_kernel kernel derivating in y-direction
 * @w_x: weights for x-direction
 * @w_y: weights for y-direction
 *
 * Fill a pyramid with derivatives.
 *
 * Returns: opencl error code
 */
cl_int oflk_pyramid_fill_derivative(oflk_pyramid_p pyramid_p,
						oflk_pyramid_p other_pyramid_p,
						cl_kernel kernel_x,
						cl_kernel kernel_y,
						cl_int4 w_x,
						cl_int4 w_y);

/**
 * oflk_pyramid_g_fill:
 * @pyramid_p: pyramid which will be filled with the matrix
 * @derivative_x_p: derivation pyramid of one image
 * @derivative_y_p: derivation pyramid of the other image
 * @kernel_g: cl_kernel computing the matrix
 *
 * Fill a pyramid with 2x2 covariance matrix on the derivatives.
 *
 * Returns: opencl error code
 */
cl_int oflk_pyramid_g_fill(oflk_pyramid_p pyramid_p,
						oflk_pyramid_p derivative_x_p,
						oflk_pyramid_p derivative_y_p,
						cl_kernel  kernel_g);

/**
 * oflk_pyramid_flow_fill:
 * @pyramid_p: pyramid which will be filled with the motion vectors
 * @img_p: pyramid holding one image's data
 * @img2_p: pyramid holding the other image's data
 * @derivative_x_p: derivation pyramid of one image
 * @derivative_y_p: derivation pyramid of the other image
 * @g_p: pyramid holding covariance matrix
 * @kernel_lkflow: cl_kernel for computing optical flow
 *
 * Fill a pyramid with motion vectors (optical flow).
 *
 * Returns: opencl error code
 */
cl_int oflk_pyramid_flow_fill(oflk_pyramid_p pyramid_p,
						oflk_pyramid_p img_p,
						oflk_pyramid_p img2_p,
						oflk_pyramid_p derivative_x_p,
						oflk_pyramid_p derivative_y_p,
						oflk_pyramid_p g_p,
						cl_kernel kernel_lkflow);


#endif /* OFLK_PYRAMID_H_ */
