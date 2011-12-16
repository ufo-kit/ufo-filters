/*
 * oflk_pyramid.c
 *
 * Pyramid structure implementation.
 *
 *  Created on: Nov 25, 2011
 *      Author: farago
 */
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include "oflk_pyramid.h"
#include "oflk_util.h"

oflk_pyramid_p oflk_pyramid_init(int levels,
						cl_channel_order channel_order,
						cl_channel_type channel_type,
						cl_context context,
						cl_command_queue command_queue,
						unsigned int width,
						unsigned int height,
						cl_int *err_num) {
	cl_mem_flags mem_flag;
	int i, sz, size;
	oflk_cl_image *image_levels;
	oflk_pyramid_p pyramid_p;

	if ((image_levels = (oflk_cl_image *) malloc(sizeof(oflk_cl_image) * levels)) == NULL) {
		return NULL;
	}
	/* sizeof(struct _oflk_pyramid) because oflk_pyramid_p is only  poniter! */
	if ((pyramid_p = (oflk_pyramid_p) malloc(sizeof(struct _oflk_pyramid))) == NULL) {
		free(image_levels);
		return NULL;
	}

	pyramid_p->context = context;
	pyramid_p->command_queue = command_queue;
	pyramid_p->levels = levels;
	pyramid_p->image_levels = image_levels;
	pyramid_p->image_format.image_channel_order = channel_order;
	pyramid_p->image_format.image_channel_data_type = channel_type;

#ifdef __APPLE__
	mem_flag = CL_MEM_READ_ONLY;
#else
	mem_flag = CL_MEM_READ_WRITE;
#endif

	/* object initialization */
	for (i = 0; i < pyramid_p->levels; i++) {
		pyramid_p->image_levels[i].width = width >> i;
		pyramid_p->image_levels[i].height = height >> i;
		pyramid_p->image_levels[i].image_format = pyramid_p->image_format;
		pyramid_p->image_levels[i].image_mem = clCreateImage2D(
										pyramid_p->context,
										mem_flag,
										&pyramid_p->image_levels[i].image_format,
										pyramid_p->image_levels[i].width,
										pyramid_p->image_levels[i].height,
										0,
										NULL,
										err_num);
		if (*err_num != CL_SUCCESS) {
			return NULL;
		}
	}

    /* initialize a scratch image on Mac, it is a linear buffer used between
       texture passes on LInux it is a R/W texture, presumably faster */
	pyramid_p->scratch_img.width = pyramid_p->image_levels[0].width;
	pyramid_p->scratch_img.height = pyramid_p->image_levels[0].height;
	pyramid_p->scratch_img.image_format = pyramid_p->image_format;
	pyramid_p->scratch_img.image_mem = clCreateImage2D(pyramid_p->context,
										mem_flag,
										&pyramid_p->scratch_img.image_format,
										pyramid_p->scratch_img.width,
										pyramid_p->scratch_img.height,
										0,
										NULL,
										err_num);
	if (*err_num != CL_SUCCESS) {
		return NULL;
	}

	/* just an absurd thing to workaround lack of texture writes */
    if(pyramid_p->image_format.image_channel_data_type == CL_UNSIGNED_INT8) {
        sz = sizeof(cl_uchar);
    } else if(pyramid_p->image_format.image_channel_data_type == CL_SIGNED_INT16) {
        sz = sizeof(cl_ushort);
    } else if(pyramid_p->image_format.image_channel_data_type == CL_SIGNED_INT32 &&
				pyramid_p->image_format.image_channel_order == CL_RGBA) {
        sz = sizeof(cl_int) * 4 ;
    } else if(pyramid_p->image_format.image_channel_data_type == CL_FLOAT &&
    			pyramid_p->image_format.image_channel_order == CL_RGBA ) {
        sz = sizeof(cl_float) * 4 ;
    } else if(pyramid_p->image_format.image_channel_data_type == CL_FLOAT) {
    	sz = sizeof(cl_float);
    } else {
        assert(0);
    }

    /* initialize scratch buffer */
    pyramid_p->scratch_buf.width = pyramid_p->image_levels[0].width;
    pyramid_p->scratch_buf.height = pyramid_p->image_levels[0].height;
    pyramid_p->scratch_buf.image_format = pyramid_p->image_format;
    size = pyramid_p->scratch_buf.width * pyramid_p->scratch_buf.height * sz;
    pyramid_p->scratch_buf.mem = clCreateBuffer(pyramid_p->context,
													CL_MEM_READ_WRITE,
													size,
													NULL,
													err_num);
    if (*err_num != CL_SUCCESS) {
    	return NULL;
    }

    return pyramid_p;
}

cl_int oflk_pyramid_release(oflk_pyramid_p *pyramid_p) {
	int i;
	cl_int err_num;

	/* release OpenCL structures */
	err_num = oflk_cl_buffer_release(&(*pyramid_p)->scratch_buf);
	err_num = oflk_cl_image_release(&(*pyramid_p)->scratch_img);
	if ((*pyramid_p)->event != NULL) {
		err_num = clReleaseEvent((*pyramid_p)->event);
	}
	for (i = 0; i < (*pyramid_p)->levels; i++) {
		err_num = oflk_cl_image_release(&(*pyramid_p)->image_levels[i]);
	}

	/* release pyramid's attributes */
	free((*pyramid_p)->image_levels);
	(*pyramid_p)->image_levels = NULL;

	/* release the pyramid */
	free(*pyramid_p);
	/* after this the variable pointing to this structure will point to NULL */
	*pyramid_p = NULL;

	return err_num;
}

cl_int oflk_pyramid_fill(oflk_pyramid_p pyramid_p,
						oflk_cl_image *oflk_image, /* changed to pointer! */
						cl_kernel downfilter_x,
						cl_kernel downfilter_y) {
	/* Adjusted variables declaration!!! See cpp version for diversions */
	cl_int err_num = CL_SUCCESS;
    size_t src_origin[3] = {0, 0, 0};
    size_t dst_origin[3] = {0, 0, 0};
    size_t region[3];
    size_t global_work_size[2];
    size_t local_work_size[2];
	int i, arg_count;

    region[0] = oflk_image->width;
    region[1] = oflk_image->height;
    region[2] = 1;

	if (oflk_image->image_format.image_channel_order != CL_INTENSITY ||
		oflk_image->image_format.image_channel_data_type != CL_FLOAT) {
		return CL_INVALID_IMAGE_FORMAT_DESCRIPTOR;
	}

	/* copy level 0 image (full size) */
	err_num = clEnqueueCopyImage(pyramid_p->command_queue,
									oflk_image->image_mem,
									pyramid_p->image_levels[0].image_mem,
									src_origin,
									dst_origin,
									region,
									0,
									NULL,
									NULL);
	RETURN_IF_CL_ERROR(err_num);

	for (i = 1; i < pyramid_p->levels; i++) {
		arg_count = 0;
		local_work_size[0] = 32;
        local_work_size[1] = 4;
        global_work_size[0] = local_work_size[0] *
        		div_up(pyramid_p->image_levels[i-1].width, local_work_size[0]) ;
        global_work_size[1] = local_work_size[1] *
        		div_up(pyramid_p->image_levels[i-1].height, local_work_size[1]) ;

        clSetKernelArg(downfilter_x, arg_count++, sizeof(cl_mem), &pyramid_p->image_levels[i-1].image_mem);
        clSetKernelArg(downfilter_x, arg_count++, sizeof(cl_mem), &pyramid_p->scratch_buf.mem);
        clSetKernelArg(downfilter_x, arg_count++, sizeof(cl_int), &pyramid_p->image_levels[i-1].width);
        clSetKernelArg(downfilter_x, arg_count++, sizeof(cl_int), &pyramid_p->image_levels[i-1].height);
        err_num = clEnqueueNDRangeKernel(pyramid_p->command_queue,
        								downfilter_x,
        								2,
        								0,
										global_work_size,
										local_work_size,
										0,
										NULL,
										&pyramid_p->event);
        RETURN_IF_CL_ERROR(err_num);

        /* perform copy from buffer to clImage
         * when image writes are not available */
        {
			size_t origin[3] = {0,0,0};
			size_t region[3];
			region[0] = pyramid_p->image_levels[i-1].width;
			region[1] = pyramid_p->image_levels[i-1].height;
			region[2] = 1;
			err_num = clEnqueueCopyBufferToImage(pyramid_p->command_queue,
											pyramid_p->scratch_buf.mem,
											pyramid_p->scratch_img.image_mem,
											0,
											origin,
											region,
											0,
											NULL,
											NULL );
			RETURN_IF_CL_ERROR(err_num);
        }

        global_work_size[0] = local_work_size[0] *
        		div_up(pyramid_p->image_levels[i].width, local_work_size[0]) ;
        global_work_size[1] = local_work_size[1] *
        		div_up(pyramid_p->image_levels[i].height, local_work_size[1]) ;
        arg_count = 0;

        /* send the imgLvl[i] texture holding teh first pass as input
         * send the scratch buffer as output */
        clSetKernelArg(downfilter_y, arg_count++, sizeof(cl_mem), &pyramid_p->scratch_img.image_mem);
        clSetKernelArg(downfilter_y, arg_count++, sizeof(cl_mem), &pyramid_p->scratch_buf.mem);
        clSetKernelArg(downfilter_y, arg_count++, sizeof(cl_int), &pyramid_p->image_levels[i].width);
        clSetKernelArg(downfilter_y, arg_count++, sizeof(cl_int), &pyramid_p->image_levels[i].height);

        err_num = clEnqueueNDRangeKernel(pyramid_p->command_queue,
											downfilter_y,
											2,
											0,
											global_work_size,
											local_work_size,
											0,
											NULL,
											&pyramid_p->event);
        RETURN_IF_CL_ERROR(err_num);

        /* perform copy from buffer to clImage
		 * when image writes are not available */
        { /* XX all wrong indexing! */
			size_t origin[3] = {0,0,0};
			size_t region[3];
			region[0] = pyramid_p->image_levels[i].width;
			region[1] = pyramid_p->image_levels[i].height;
			region[2] = 1;
			err_num = clEnqueueCopyBufferToImage(pyramid_p->command_queue,
					pyramid_p->scratch_buf.mem,
					pyramid_p->image_levels[i].image_mem,
					0,
					origin,
					region,
					0,
					NULL,
					NULL );
			RETURN_IF_CL_ERROR(err_num);
        }
    }

	return err_num;
}

cl_int oflk_pyramid_fill_derivative(oflk_pyramid_p pyramid_p,
						oflk_pyramid_p other_pyramid_p, /* changed to pointer! */
						cl_kernel kernel_x,
						cl_kernel kernel_y,
						cl_int4 w_x,
						cl_int4 w_y) {
    cl_int err_num = CL_SUCCESS;
    size_t global_work_size[2];
    size_t local_work_size[2];
    int i, arg_count;

    if (other_pyramid_p->levels != 3 ||
		other_pyramid_p->image_format.image_channel_order != CL_INTENSITY ||
		other_pyramid_p->image_format.image_channel_data_type != CL_FLOAT) {
    	return OFLK_INVALID_PYRAMID_TYPE;
    }

    for (i = 0; i < pyramid_p->levels; i++) {
    	arg_count = 0;
        local_work_size[0] = 32;
        local_work_size[1] = 4;

        global_work_size[0] = local_work_size[0] *
        		div_up(pyramid_p->image_levels[i].width, local_work_size[0]) ;
        global_work_size[1] = local_work_size[1] *
        		div_up(pyramid_p->image_levels[i].height, local_work_size[1]) ;

        clSetKernelArg(kernel_x, arg_count++, sizeof(cl_mem), &other_pyramid_p->image_levels[i].image_mem);
        clSetKernelArg(kernel_x, arg_count++, sizeof(cl_mem), &pyramid_p->scratch_buf.mem);
        clSetKernelArg(kernel_x, arg_count++, sizeof(cl_int), &other_pyramid_p->image_levels[i].width);
        clSetKernelArg(kernel_x, arg_count++, sizeof(cl_int), &other_pyramid_p->image_levels[i].height);
		clSetKernelArg(kernel_x, arg_count++, sizeof(cl_int), &w_x.s[0]);
		clSetKernelArg(kernel_x, arg_count++, sizeof(cl_int), &w_x.s[1]);
		clSetKernelArg(kernel_x, arg_count++, sizeof(cl_int), &w_x.s[2]);
        err_num = clEnqueueNDRangeKernel(pyramid_p->command_queue,
											kernel_x,
											2,
											0,
											global_work_size,
											local_work_size,
											0,
											NULL,
											&pyramid_p->event);
        RETURN_IF_CL_ERROR(err_num);

        /* perform copy from buffer to clImage
		 * when image writes are not available */
        {
        size_t origin[3] = {0,0,0};
        size_t region[3];
        region[0] = other_pyramid_p->image_levels[i].width;
        region[1] = other_pyramid_p->image_levels[i].height;
        region[2] = 1;
        err_num = clEnqueueCopyBufferToImage(
        		pyramid_p->command_queue,
        		pyramid_p->scratch_buf.mem,
        		pyramid_p->scratch_img.image_mem,
        		0,
        		origin,
        		region,
        		0,
        		NULL,
        		NULL );
        RETURN_IF_CL_ERROR(err_num);
        }

        arg_count = 0;
        clSetKernelArg(kernel_y, arg_count++, sizeof(cl_mem), &pyramid_p->scratch_img.image_mem);
        clSetKernelArg(kernel_y, arg_count++, sizeof(cl_mem), &pyramid_p->scratch_buf.mem);
        clSetKernelArg(kernel_y, arg_count++, sizeof(cl_int), &other_pyramid_p->image_levels[i].width);
        clSetKernelArg(kernel_y, arg_count++, sizeof(cl_int), &other_pyramid_p->image_levels[i].height);
        clSetKernelArg(kernel_y, arg_count++, sizeof(cl_int), &w_y.s[0]);
        clSetKernelArg(kernel_y, arg_count++, sizeof(cl_int), &w_y.s[1]);
        clSetKernelArg(kernel_y, arg_count++, sizeof(cl_int), &w_y.s[2]);
        err_num = clEnqueueNDRangeKernel(pyramid_p->command_queue,
														kernel_y,
														2,
														0,
														global_work_size,
														local_work_size,
														0,
														NULL,
														&pyramid_p->event);

        RETURN_IF_CL_ERROR(err_num);

        /* perform copy from buffer to clImage
		 * when image writes are not available */
        {
			size_t origin[3] = {0,0,0};
	        size_t region[3];
	        region[0] = other_pyramid_p->image_levels[i].width;
	        region[1] = other_pyramid_p->image_levels[i].height;
	        region[2] = 1;
			err_num = clEnqueueCopyBufferToImage(pyramid_p->command_queue,
										pyramid_p->scratch_buf.mem,
										pyramid_p->image_levels[i].image_mem,
										0,
										origin,
										region,
										0,
										NULL,
										NULL);
			RETURN_IF_CL_ERROR(err_num);
        }
    }

    return err_num;
}

cl_int oflk_pyramid_g_fill(oflk_pyramid_p pyramid_p, /* changed to pointer! */
				oflk_pyramid_p derivative_x_p, /* changed to pointer from reference! */
				oflk_pyramid_p derivative_y_p, /* changed to pointer from reference! */
				cl_kernel  kernel_g) {
    cl_int err_num = CL_SUCCESS;
    size_t global_work_size[2];
    size_t local_work_size[2];
    int i, arg_count;

    if (derivative_x_p->levels != 3 ||
		derivative_x_p->image_format.image_channel_order != CL_INTENSITY ||
		derivative_x_p->image_format.image_channel_data_type != CL_FLOAT) {
    	return OFLK_INVALID_PYRAMID_TYPE;
    }
    if (derivative_y_p->levels != 3 ||
		derivative_y_p->image_format.image_channel_order != CL_INTENSITY ||
		derivative_y_p->image_format.image_channel_data_type != CL_FLOAT) {
    	return OFLK_INVALID_PYRAMID_TYPE;
    }

    for (i = 0; i < pyramid_p->levels; i++) {
    	arg_count = 0;
        local_work_size[0] = 32;
        local_work_size[1] = 4;

        global_work_size[0] = local_work_size[0] *
        		div_up(pyramid_p->image_levels[i].width, local_work_size[0]);
        global_work_size[1] = local_work_size[1] *
        		div_up(pyramid_p->image_levels[i].height, local_work_size[1]);

        clSetKernelArg(kernel_g, arg_count++, sizeof(cl_mem), &derivative_x_p->image_levels[i].image_mem);
        clSetKernelArg(kernel_g, arg_count++, sizeof(cl_mem), &derivative_y_p->image_levels[i].image_mem);
        clSetKernelArg(kernel_g, arg_count++, sizeof(cl_mem), &pyramid_p->scratch_buf.mem);
        clSetKernelArg(kernel_g, arg_count++, sizeof(cl_uint), &derivative_y_p->image_levels[i].width);
        clSetKernelArg(kernel_g, arg_count++, sizeof(cl_uint), &derivative_y_p->image_levels[i].height);
        err_num = clEnqueueNDRangeKernel(pyramid_p->command_queue,
        									kernel_g,
											2,
											0,
											global_work_size,
											local_work_size,
											0,
											NULL,
											&pyramid_p->event);
        RETURN_IF_CL_ERROR(err_num);

        {
        size_t origin[3] = {0,0,0};
        size_t region[3];
        region[0] = pyramid_p->image_levels[i].width;
        region[1] = pyramid_p->image_levels[i].height;
        region[2] = 1;
        err_num = clEnqueueCopyBufferToImage(
        		pyramid_p->command_queue,
        		pyramid_p->scratch_buf.mem,
        		pyramid_p->image_levels[i].image_mem,
        		0,
        		origin,
        		region,
        		0,
        		NULL,
        		NULL );
        RETURN_IF_CL_ERROR(err_num);
        }
    }

    return err_num;
}

cl_int oflk_pyramid_flow_fill(oflk_pyramid_p pyramid_p, /* changed to pointer! */
						oflk_pyramid_p img_p, /* ALL PARAMETERS changed to pointer from reference! */
						oflk_pyramid_p img2_p,
						oflk_pyramid_p derivative_x_p,
						oflk_pyramid_p derivative_y_p,
						oflk_pyramid_p g_p,
						cl_kernel kernel_lkflow) {
    cl_int err_num = CL_SUCCESS;
    size_t global_work_size[2];
    size_t local_work_size[2];
    int i, arg_count, use_guess = 0;

    if (img_p->levels != 3 ||
		img_p->image_format.image_channel_order != CL_INTENSITY ||
		img_p->image_format.image_channel_data_type != CL_FLOAT) {
    	return OFLK_INVALID_PYRAMID_TYPE;
    }
    if (img2_p->levels != 3 ||
		img2_p->image_format.image_channel_order != CL_INTENSITY ||
		img2_p->image_format.image_channel_data_type != CL_FLOAT) {
    	return OFLK_INVALID_PYRAMID_TYPE;
    }
    if (derivative_x_p->levels != 3 ||
		derivative_x_p->image_format.image_channel_order != CL_INTENSITY ||
		derivative_x_p->image_format.image_channel_data_type != CL_FLOAT) {
    	return OFLK_INVALID_PYRAMID_TYPE;
    }
    if (derivative_y_p->levels != 3 ||
		derivative_y_p->image_format.image_channel_order != CL_INTENSITY ||
		derivative_y_p->image_format.image_channel_data_type != CL_FLOAT) {
    	return OFLK_INVALID_PYRAMID_TYPE;
    }
    if (g_p->levels != 3 ||
		g_p->image_format.image_channel_order != CL_RGBA ||
		g_p->image_format.image_channel_data_type != CL_FLOAT) {
    	return OFLK_INVALID_PYRAMID_TYPE;
    }

    /* beginning at the top level work down the base (largest) */
    for (i = pyramid_p->levels - 1; i >= 0; i--) {
    	arg_count = 0;
    	if (i < pyramid_p->levels - 1) {
    		use_guess = 1;
    	}
        local_work_size[0] = 16;
        local_work_size[1] = 8;

        global_work_size[0] = local_work_size[0] *
        		div_up(pyramid_p->image_levels[i].width, local_work_size[0]);
        global_work_size[1] = local_work_size[1] *
        		div_up(pyramid_p->image_levels[i].height, local_work_size[1]);

        clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_mem), &img_p->image_levels[i].image_mem);
        clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_mem), &img2_p->image_levels[i].image_mem);
        clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_mem), &derivative_x_p->image_levels[i].image_mem);
        clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_mem), &derivative_y_p->image_levels[i].image_mem);
        clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_mem), &g_p->image_levels[i].image_mem);
        if (use_guess) {
        	/* send previous level guesses if available */
        	clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_mem), &pyramid_p->image_levels[i+1].image_mem);
        } else {
        	clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_mem), &pyramid_p->scratch_img.image_mem);
        }
        clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_mem), &pyramid_p->image_levels[i].image_mem);
        clSetKernelArg(kernel_lkflow, arg_count++, sizeof(cl_int), &use_guess);

        err_num = clEnqueueNDRangeKernel(pyramid_p->command_queue,
        									kernel_lkflow,
											2,
											0,
											global_work_size,
											local_work_size,
											0,
											NULL,
											&pyramid_p->event);
        RETURN_IF_CL_ERROR(err_num);
    }

    return err_num;
}






