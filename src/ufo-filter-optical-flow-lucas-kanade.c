#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <stdio.h>
#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-optical-flow-lucas-kanade.h"
#include "oflk_cl_buffer.h"
#include "oflk_cl_image.h"
#include "oflk_pyramid.h"
#include "oflk_util.h"

#define LEVELS 3

/**
 * SECTION:ufo-filter-optical-flow-lucas-kanade
 * @Short_description: Compute the optical flow
 * @Title: opticalflowlucaskanade
 *
 * Processes two adjacent input images and computes the motion vectors between
 * them. The output is an image with twice the width.
 */

struct _UfoFilterOpticalFlowLucasKanadePrivate {
    /* add your private data here */
	cl_kernel downfilter_x_kernel;
	cl_kernel downfilter_y_kernel;
	cl_kernel filter_3x1_kernel;
	cl_kernel filter_1x3_kernel;
	cl_kernel filter_g_kernel;
	cl_kernel lkflow_kernel;
	cl_kernel update_motion_kernel;

	oflk_pyramid_p img_p;
	oflk_pyramid_p img2_p;
	oflk_pyramid_p derivative_x_p;
	oflk_pyramid_p derivative_y_p;
	oflk_pyramid_p g_p;
	oflk_cl_buffer flow_levels[LEVELS];
	oflk_cl_image old_image;
	oflk_cl_image new_image;

    cl_int4 dx_Wx;
    cl_int4 dx_Wy;
    cl_int4 dy_Wx;
    cl_int4 dy_Wy;
};

G_DEFINE_TYPE(UfoFilterOpticalFlowLucasKanade, ufo_filter_optical_flow_lucas_kanade, UFO_TYPE_FILTER)

#define UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_OPTICAL_FLOW_LUCAS_KANADE, UfoFilterOpticalFlowLucasKanadePrivate))

enum {
    PROP_0,
    N_PROPERTIES
};


// TODO supporting function for outputting images in text format, remove later
/* static void save_octave_intensity_float( oflk_cl_image img, cl_command_queue cmdq, const char *fname ) { */
/* 	unsigned int j, i; */
/* 	float *h_img_ub; */
/* 	size_t origin[3] = {0,0,0}, region[3]; */
/* 	FILE *fd; */

/* 	if (img.image_format.image_channel_order != CL_INTENSITY || */
/* 			img.image_format.image_channel_data_type != CL_FLOAT) { */
/* 		fprintf(stderr, "Wrong image format.\n"); */
/* 		exit(1); */
/* 	} */

/* 	h_img_ub = (float *) malloc(sizeof(float) * img.width * img.height) ; */
/*     region[0] = img.width; */
/*     region[1] = img.height; */
/*     region[2] = 1; */

/*     CHECK_OPENCL_ERROR(clEnqueueReadImage(cmdq, */
/* 								img.image_mem, */
/* 								CL_TRUE, */
/* 								origin, */
/* 								region, */
/* 								0, */
/* 								0, */
/* 								(void *) h_img_ub, */
/* 								0, */
/* 								NULL, */
/* 								NULL)); */

/*     fd = fopen(fname, "w"); */
/*     if (fd != NULL) { */
/*         for ( j=0 ; j<img.height ; j++ ) { */
/*             for ( i=0 ; i<img.width ; i++ ) { */
/*                 fprintf(fd, "%f ", h_img_ub[j*img.width+i] ); */
/*             } */
/*             fprintf(fd, "\n"); */
/*         } */
/*         fclose(fd); */
/*         printf("Wrote: %s\n", fname); */
/*     } */
/*     free( h_img_ub ); */
/* } */

// TODO supporting function for outputting motion field in text format, remove later
/* static void save_octave_float2(oflk_cl_buffer img, cl_command_queue cmdq, const char *fname ) { */
/*     unsigned int j, i; */
/*     FILE *fd; */
/*     cl_float *h_img_float2 = (cl_float *)malloc( (img.width) * img.height * sizeof( cl_float) * 2 ) ; */

/*     printf("%s : reading stride: %d->%lu\n", fname, img.width, img.width*sizeof(cl_float) * 2); */

/*     CHECK_OPENCL_ERROR(clEnqueueReadBuffer( cmdq, img.mem, CL_TRUE, */
/*         0, img.width*img.height*sizeof(cl_float)*2, h_img_float2, 0, NULL, NULL )); */


/*     fd = fopen(fname, "w"); */
/*     if (fd != NULL) { */
/*          for(j=0 ; j<img.height ; j++ ) { */
/*             for(i=0 ; i<img.width ; i+=2 ) { */
/*                 fprintf(fd, "%f ", h_img_float2[j*img.width+i] ); */
/*                 fprintf(fd, "%f ", h_img_float2[j*img.width+i+1] ); */
/*             } */
/*             fprintf(fd, "\n"); */
/*         } */
/*         fclose(fd); */
/*         printf("Wrote: %s\n", fname); */
/*     } else { */
/*     	perror(fname); */
/*     	exit(1); */
/*     } */

/*     free(h_img_float2); */
/* } */

static cl_int oflk_flow_init(cl_context context,
							cl_command_queue command_queue,
							oflk_pyramid_p *img_p,
							oflk_pyramid_p *img2_p,
							oflk_pyramid_p *derivative_x_p,
							oflk_pyramid_p *derivative_y_p,
							oflk_pyramid_p *g_p,
							oflk_cl_buffer flow_levels[LEVELS],
							oflk_cl_image *image_data1_p,
							oflk_cl_image *image_data2_p,
							guint width,
							guint height) {
    cl_int err_num = CL_SUCCESS;
	gint i;
    gsize size;

    /* initialize structure holding image data, static structures, thus no
     * memory allocation for them!
     */
    image_data1_p->width = width;
    image_data1_p->height = height;
    image_data1_p->image_format.image_channel_order = CL_INTENSITY;
    image_data1_p->image_format.image_channel_data_type = CL_FLOAT;
    image_data1_p->image_mem = clCreateImage2D(context,
    												CL_MEM_READ_WRITE,
    												&image_data1_p->image_format,
    												width,
    												height,
    												0,
    												NULL,
    												&err_num);
    RETURN_IF_CL_ERROR(err_num);

    image_data2_p->width = width;
    image_data2_p->height = height;
    image_data2_p->image_format.image_channel_order = CL_INTENSITY;
    image_data2_p->image_format.image_channel_data_type = CL_FLOAT;
    image_data2_p->image_mem = clCreateImage2D(context,
    												CL_MEM_READ_WRITE,
    												&image_data2_p->image_format,
    												width,
    												height,
    												0,
    												NULL,
    												&err_num);
    RETURN_IF_CL_ERROR(err_num);

    /* create pyramids */
    *img_p = oflk_pyramid_init(3,
								CL_INTENSITY,
								CL_FLOAT,
								context,
								command_queue,
								width,
								height,
								&err_num);
    RETURN_IF_CL_ERROR(err_num);

    *img2_p = oflk_pyramid_init(3,
								CL_INTENSITY,
								CL_FLOAT,
								context,
								command_queue,
								width,
								height,
								&err_num);
    RETURN_IF_CL_ERROR(err_num);

    *derivative_x_p = oflk_pyramid_init(3,
										CL_INTENSITY,
										CL_FLOAT,
										context,
										command_queue,
										width,
										height,
										&err_num);
    RETURN_IF_CL_ERROR(err_num);

    *derivative_y_p = oflk_pyramid_init(3,
										CL_INTENSITY,
										CL_FLOAT,
										context,
										command_queue,
										width,
										height,
										&err_num);
    RETURN_IF_CL_ERROR(err_num);

    *g_p = oflk_pyramid_init(3,
							CL_RGBA,
							CL_FLOAT,
							context,
							command_queue,
							width,
							height,
							&err_num);
    RETURN_IF_CL_ERROR(err_num);

    /* simulate a CL_RG buffer in global memory, for lack of support for CL_RG */
    for (i = 0; i < 3; i++) {
    	flow_levels[i].width = i == 0 ? width << 1 : width >> (i - 1);
    	flow_levels[i].height = height >> i;
    	flow_levels[i].image_format.image_channel_data_type = CL_FLOAT;
    	flow_levels[i].image_format.image_channel_order = CL_INTENSITY;
        /* *2 because of dx and dy components of a motion vector */
    	size = (gsize) flow_levels[i].width * flow_levels[i].height * sizeof(cl_float) * 2;
        flow_levels[i].mem = clCreateBuffer(context,
												CL_MEM_READ_WRITE,
												size,
												NULL,
												&err_num );
        RETURN_IF_CL_ERROR(err_num);
    }

    return err_num;
}

static cl_int oflk_flow_release(oflk_pyramid_p *img_p,
					oflk_pyramid_p *img2_p,
					oflk_pyramid_p *derivative_x_p,
					oflk_pyramid_p *derivative_y_p,
					oflk_pyramid_p *g_p,
					oflk_cl_buffer flow_levels[LEVELS]) {
	int i;

	RETURN_IF_CL_ERROR(oflk_pyramid_release(img_p));
	RETURN_IF_CL_ERROR(oflk_pyramid_release(img2_p));
	RETURN_IF_CL_ERROR(oflk_pyramid_release(derivative_x_p));
	RETURN_IF_CL_ERROR(oflk_pyramid_release(derivative_y_p));
	RETURN_IF_CL_ERROR(oflk_pyramid_release(g_p));

	for (i = 0; i < LEVELS; i++) {
		RETURN_IF_CL_ERROR(clReleaseMemObject(flow_levels[i].mem));
	}

	return CL_SUCCESS;
}

static cl_int oflk_flow_calc_flow(oflk_cl_buffer flow_levels[LEVELS],
								cl_kernel lkflow_kernel,
								cl_command_queue command_queue,
								oflk_pyramid_p img_p,
								oflk_pyramid_p img2_p,
								oflk_pyramid_p derivative_x_p,
								oflk_pyramid_p derivative_y_p,
								oflk_pyramid_p g_p,
								cl_event *flow_event) {
    int i;
    cl_int err_num = CL_SUCCESS;
    size_t global_work_size[2];
    size_t local_work_size[2];

    /* beginning at the top level work down the base (largest) */
    for(i = LEVELS-1; i >= 0 ; i--) {
        cl_uint arg_count = 0;
        int use_guess = 0;
        if( i <LEVELS-1 ) use_guess = 1;

        local_work_size[0] =16;
        local_work_size[1] = 8;

        global_work_size[0] = local_work_size[0] * div_up(img_p->image_levels[i].width, local_work_size[0]) ;
        global_work_size[1] = local_work_size[1] * div_up(img_p->image_levels[i].height, local_work_size[1]) ;

        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_mem), &img_p->image_levels[i].image_mem);
        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_mem), &derivative_x_p->image_levels[i].image_mem);
        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_mem), &derivative_y_p->image_levels[i].image_mem);
        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_mem), &g_p->image_levels[i].image_mem);
        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_mem), &img2_p->image_levels[i].image_mem);

        if(use_guess) {
        	/* send previous level guesses if available */
            clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_mem), &flow_levels[i+1].mem);
            clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_uint), &flow_levels[i+1].width);
        } else {
        	/* if no previous level just send irrelevant pointer */
            clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_mem), &flow_levels[0].mem);
            clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_uint), &flow_levels[0].width);
        }

        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_mem), &flow_levels[i].mem);
        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_uint), &flow_levels[i].width);
        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_uint), &flow_levels[i].height);
        clSetKernelArg(lkflow_kernel, arg_count++, sizeof(cl_int), &use_guess);

        RETURN_IF_CL_ERROR(clEnqueueNDRangeKernel(command_queue,
											lkflow_kernel,
											2,
											0,
											global_work_size,
											local_work_size,
											0,
											NULL,
											flow_event));
    }

    return err_num;
}

static void ufo_filter_optical_flow_lucas_kanade_initialize(UfoFilter *filter, UfoBuffer *params[])
{
    /* Here you can code, that is called for each newly instantiated filter */
	UfoFilterOpticalFlowLucasKanade *self = UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->downfilter_x_kernel = NULL;
    self->priv->downfilter_y_kernel = NULL;
    self->priv->filter_3x1_kernel = NULL;
    self->priv->filter_1x3_kernel = NULL;
    self->priv->filter_g_kernel = NULL;
    self->priv->lkflow_kernel = NULL;
    self->priv->update_motion_kernel = NULL;

    ufo_resource_manager_add_paths(manager, "/home/farago/workspace/C++/filtertest/Debug"); // a must since ufo-core revision 323

    self->priv->downfilter_x_kernel = ufo_resource_manager_get_kernel(manager, "filters.cl", "downfilter_x_g", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
    self->priv->downfilter_y_kernel = ufo_resource_manager_get_kernel(manager, "filters.cl", "downfilter_y_g", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
    self->priv->filter_3x1_kernel = ufo_resource_manager_get_kernel(manager, "filters.cl", "filter_3x1_g", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
    self->priv->filter_1x3_kernel = ufo_resource_manager_get_kernel(manager, "filters.cl", "filter_1x3_g", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
    self->priv->filter_g_kernel = ufo_resource_manager_get_kernel(manager, "filters.cl", "filter_G", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
    self->priv->lkflow_kernel = ufo_resource_manager_get_kernel(manager, "lkflow.cl", "lkflow", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
    self->priv->update_motion_kernel = ufo_resource_manager_get_kernel(manager, "motion.cl", "motion", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }

    self->priv->dx_Wx.s[0] = -1;
    self->priv->dx_Wx.s[1] = 0;
    self->priv->dx_Wx.s[2] = 1;
    self->priv->dx_Wx.s[3] = 0;

    self->priv->dx_Wy.s[0] = 3;
    self->priv->dx_Wy.s[1] = 10;
    self->priv->dx_Wy.s[2] = 3;
    self->priv->dx_Wy.s[3] = 0;

    self->priv->dy_Wx.s[0] = 3;
    self->priv->dy_Wx.s[1] = 10;
    self->priv->dy_Wx.s[2] = 3;
    self->priv->dy_Wx.s[3] = 0;

    self->priv->dy_Wy.s[0] = -1;
    self->priv->dy_Wy.s[1] = 0;
    self->priv->dy_Wy.s[2] = 1;
    self->priv->dy_Wy.s[3] = 0;
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_optical_flow_lucas_kanade_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterOpticalFlowLucasKanade *self = UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE(filter);
	UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
	UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
	UfoBuffer *image_buffer = ufo_channel_get_input_buffer(input_channel);

    if (image_buffer == NULL)
        return;

	UfoBuffer *motion_vectors_buffer = NULL;
	UfoResourceManager *manager = ufo_resource_manager();
	cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
	cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);
	cl_int err_num = CL_SUCCESS;
	cl_mem tmp_buf; //tmp buffer for copying into image cl_mem
	size_t tmp_origin[3] = {0, 0, 0};
	size_t tmp_region[3];
	int i = 0;
	gboolean buffers_initialized = FALSE;

	/* If you provide any output, you must allocate output buffers of the
	   appropriate size */
    guint num_dims = 0;
    guint *dimensions = NULL;

	ufo_buffer_get_dimensions(image_buffer, &num_dims, &dimensions);
	guint output_dimensions[2] = { dimensions[0] * 2, dimensions[1] };
	tmp_region[0] = dimensions[0];
	tmp_region[1] = dimensions[1];
	tmp_region[2] = 1;

    size_t num_bytes = output_dimensions[0] * output_dimensions[1] * sizeof(float);

	// Optical flow initialization
	CHECK_OPENCL_ERROR(oflk_flow_init(context,
					command_queue,
					&self->priv->img_p,
					&self->priv->img2_p,
					&self->priv->derivative_x_p,
					&self->priv->derivative_y_p,
					&self->priv->g_p,
					self->priv->flow_levels,
					&self->priv->old_image,
					&self->priv->new_image,
					dimensions[0],
					dimensions[1]));

	while (image_buffer != NULL) {
		cl_event flow_event; // event for Lucas Kanade kernel execution

		/* Copy buffer obtained from UfoBuffer to an image for further
		 * processing. Switch between two temporary storages for being
		 * able to access an "old" image and a "new" image in one cycle.
		 */
		tmp_buf = (cl_mem) ufo_buffer_get_device_array(image_buffer, command_queue);
		CHECK_OPENCL_ERROR(clEnqueueCopyBufferToImage(command_queue,
										tmp_buf,
										i % 2 == 0 ? self->priv->new_image.image_mem :
												self->priv->old_image.image_mem,
										0,
										tmp_origin,
										tmp_region,
										0,
										NULL,
										NULL));

		if (i > 0) {
			if (!buffers_initialized) {
				ufo_channel_allocate_output_buffers(output_channel, 2, output_dimensions);
				buffers_initialized = TRUE;
			}
			motion_vectors_buffer = ufo_channel_get_output_buffer(output_channel);

			// optical flow preprocessing computation
			err_num = oflk_pyramid_fill(self->priv->img_p,
										i % 2 == 0 ? &self->priv->new_image : &self->priv->old_image,
										self->priv->downfilter_x_kernel,
										self->priv->downfilter_y_kernel);
			CHECK_OPENCL_ERROR(err_num);

			err_num = oflk_pyramid_fill(self->priv->img2_p,
										i % 2 == 0 ? &self->priv->old_image : &self->priv->new_image,
										self->priv->downfilter_x_kernel,
										self->priv->downfilter_y_kernel);
			CHECK_OPENCL_ERROR(err_num);

			err_num = oflk_pyramid_fill_derivative(self->priv->derivative_x_p,
													self->priv->img_p,
													self->priv->filter_3x1_kernel,
													self->priv->filter_1x3_kernel,
													self->priv->dx_Wx,
													self->priv->dx_Wy);
			CHECK_OPENCL_ERROR(err_num);

			err_num = oflk_pyramid_fill_derivative(self->priv->derivative_y_p,
													self->priv->img_p,
													self->priv->filter_3x1_kernel,
													self->priv->filter_1x3_kernel,
													self->priv->dy_Wx,
													self->priv->dy_Wy);
			CHECK_OPENCL_ERROR(err_num);

			err_num = oflk_pyramid_g_fill(self->priv->g_p,
											self->priv->derivative_x_p,
											self->priv->derivative_y_p,
											self->priv->filter_g_kernel);
			CHECK_OPENCL_ERROR(err_num);
			// end of optical flow preprocessing computation

			// Optical flow Lucas Kanade computation
			CHECK_OPENCL_ERROR(oflk_flow_calc_flow(self->priv->flow_levels,
								self->priv->lkflow_kernel,
								command_queue,
								self->priv->img_p,
								self->priv->img2_p,
								self->priv->derivative_x_p,
								self->priv->derivative_y_p,
								self->priv->g_p,
								&flow_event));

  			ufo_filter_account_gpu_time(filter, (void **) &flow_event);

			/* result is the topmost flow pyramid */
			// GPU memory transfer, does not work
            cl_event event;
  			ufo_buffer_transfer_id(image_buffer, motion_vectors_buffer);
            cl_mem motion_mem = ufo_buffer_get_device_array(motion_vectors_buffer, command_queue);
            CHECK_OPENCL_ERROR(clEnqueueCopyBuffer(command_queue,
                    self->priv->flow_levels[0].mem, motion_mem,
                    0, 0, num_bytes,
                    1, &flow_event, &event));

            /* Actually, instead of using clWaitForEvents() we should attach the
             * event to motion_vectors_buffer. However, the same event must be
             * also used to synchronize further accesses to flow_levels[0].mem
             * in order to prevent writes during reads. */
            clWaitForEvents(1, &event);
			// end of GPU memory transfer, does not work

			ufo_channel_finalize_output_buffer(output_channel, motion_vectors_buffer);
		}

		/* Get new image */
		ufo_channel_finalize_input_buffer(input_channel, image_buffer);
		image_buffer = ufo_channel_get_input_buffer(input_channel);
		i++;
	}

	/* release memory used by OF */
	CHECK_OPENCL_ERROR(oflk_flow_release(&self->priv->img_p,
									&self->priv->img2_p,
									&self->priv->derivative_x_p,
									&self->priv->derivative_y_p,
									&self->priv->g_p,
									self->priv->flow_levels));

	/* release temporary memory */
	CHECK_OPENCL_ERROR(clReleaseMemObject(self->priv->old_image.image_mem));
	CHECK_OPENCL_ERROR(clReleaseMemObject(self->priv->new_image.image_mem));
	CHECK_OPENCL_ERROR(clReleaseMemObject(tmp_buf));

	printf(">>>>> Releasing output channel <<<<<\n");
	/* Tell subsequent filters, that we are finished */
	ufo_channel_finish(output_channel);
	printf(">>>>> Output channel released <<<<<\n");
    g_free(dimensions);
}

static void ufo_filter_optical_flow_lucas_kanade_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_optical_flow_lucas_kanade_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_optical_flow_lucas_kanade_class_init(UfoFilterOpticalFlowLucasKanadeClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_optical_flow_lucas_kanade_set_property;
    gobject_class->get_property = ufo_filter_optical_flow_lucas_kanade_get_property;
    filter_class->initialize = ufo_filter_optical_flow_lucas_kanade_initialize;
    filter_class->process = ufo_filter_optical_flow_lucas_kanade_process;

    g_type_class_add_private(gobject_class, sizeof(UfoFilterOpticalFlowLucasKanadePrivate));
}

static void ufo_filter_optical_flow_lucas_kanade_init(UfoFilterOpticalFlowLucasKanade *self)
{
    self->priv = UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE_GET_PRIVATE(self);

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output0", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_OPTICAL_FLOW_LUCAS_KANADE, NULL);
}
