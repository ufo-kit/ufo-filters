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

struct _oflk_pyramid {
	int levels; /** number of image pyramid levels */
	cl_image_format image_format;
	oflk_cl_image *image_levels; /** array of oflk_cl_image structures holding data for different levels */
    oflk_cl_buffer scratch_buf; /** tmp buffer */
    oflk_cl_image scratch_img; /** tmp image */
    cl_context context; /** opencl context */
    cl_command_queue command_queue; /** opencl command queue */
    cl_event event; /** opencl event for timing information */
};

/**
 * Initialize pyramid.
 *
 * \param levels number of pyramid levels
 * \param channel_order channel order
 * \param channel_type channel_type
 * \param context cl_context
 * \param command_queue cl_command_queue
 * \param width width of an image at the 0th level (not undersampled)
 * \param height height of an image at the 0th level (not undersampled)
 * \param *err_num pointer to cl_int error number
 *
 * \return pointer to oflk_pyramid
 */
oflk_pyramid_p oflk_pyramid_init(int levels,
						cl_channel_order channel_order,
						cl_channel_type channel_type,
						cl_context context,
						cl_command_queue command_queue,
						unsigned int width,
						unsigned int height,
						cl_int *err_num);

/**
 * Release specified pyramid.
 *
 * \param *pyramid_p pointer to a pointer to the pyramid to be released. Double
 * 			pointer enables us to make the actual pointer to the pyramid
 * 			to point to a NULL structure.
 *
 * \return 0 on success, opencl error code caused by releasing obejcts otherwise
 */
cl_int oflk_pyramid_release(oflk_pyramid_p *pyramid_p);

/**
 * Fill a pyramid with subsampled levels.
 *
 * \param pyramid_p pointer to a pyramid
 * \param oflk_image oflk_cl_image structure holidng the data
 * \param downfilter_x cl_kernel subsampler in x direction
 * \param downfilter_x cl_kernel subsampler in y direction
 *
 * \return opencl error code
 */
cl_int oflk_pyramid_fill(oflk_pyramid_p pyramid_p,
						oflk_cl_image *oflk_image,
						cl_kernel downfilter_x,
						cl_kernel downfilter_y);

/**
 * Fill a pyramid with derivatives.
 *
 * \param pyramid_p pyramid which will be filled with derivatves
 * \param other_pyramid_p pyramid from which the derivatives will be calculated
 * \param kernel_x cl_kernel kernel derivating in x-direction
 * \param kernel_y cl_kernel kernel derivating in y-direction
 * \param w_x weights for x-direction
 * \param w_y weights for y-direction
 *
 * \return opencl error code
 */
cl_int oflk_pyramid_fill_derivative(oflk_pyramid_p pyramid_p,
						oflk_pyramid_p other_pyramid_p,
						cl_kernel kernel_x,
						cl_kernel kernel_y,
						cl_int4 w_x,
						cl_int4 w_y);

/**
 * Fill a pyramid with 2x2 covariance matrix on the derivatives.
 *
 * \param pyramid_p pyramid which will be filled with the matrix
 * \param derivative_x_p derivation pyramid of one image
 * \param derivative_y_p derivation pyramid of the other image
 * \param kernel_g cl_kernel computing the matrix
 *
 * \return opencl error code
 */
cl_int oflk_pyramid_g_fill(oflk_pyramid_p pyramid_p,
						oflk_pyramid_p derivative_x_p,
						oflk_pyramid_p derivative_y_p,
						cl_kernel  kernel_g);

/**
 * Fill a pyramid with motion vectors (optical flow).
 *
 * \param pyramid_p pyramid which will be filled with the motion vectors
 * \param img_p pyramid holding one image's data
 * \param img2_p pyramid holding the other image's data
 * \param derivative_x_p derivation pyramid of one image
 * \param derivative_y_p derivation pyramid of the other image
 * \param g_p pyramid holding covariance matrix
 * \param kernel_lkflow cl_kernel for computing optical flow
 *
 * \return opencl error code
 */
cl_int oflk_pyramid_flow_fill(oflk_pyramid_p pyramid_p,
						oflk_pyramid_p img_p,
						oflk_pyramid_p img2_p,
						oflk_pyramid_p derivative_x_p,
						oflk_pyramid_p derivative_y_p,
						oflk_pyramid_p g_p,
						cl_kernel kernel_lkflow);


#endif /* OFLK_PYRAMID_H_ */
