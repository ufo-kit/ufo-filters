#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif
#include <clFFT.h>

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-fft.h"

/**
 * SECTION:ufo-filter-fft
 * @Short_description: Compute discrete Fourier transform
 * @Title: fft
 *
 * Compute discrete Fourier transform using Apples OpenCL FFT library that is
 * provides as liboclfft.
 */

struct _UfoFilterFFTPrivate {
    cl_kernel kernel;
    clFFT_Dimension fft_dimensions;
    clFFT_Dim3 fft_size;
};

G_DEFINE_TYPE(UfoFilterFFT, ufo_filter_fft, UFO_TYPE_FILTER)

#define UFO_FILTER_FFT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_FFT, UfoFilterFFTPrivate))

enum {
    PROP_0,
    PROP_DIMENSIONS,
    PROP_SIZE_X,
    PROP_SIZE_Y,
    PROP_SIZE_Z,
    N_PROPERTIES
};

static GParamSpec *fft_properties[N_PROPERTIES] = { NULL, };

static guint32 pow2round(guint32 x)
{
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x+1;
}

static void ufo_filter_fft_initialize(UfoFilter *filter, UfoBuffer *params[])
{
    UfoFilterFFT *self = UFO_FILTER_FFT(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->kernel = ufo_resource_manager_get_kernel(manager, "fft.cl", "fft_spread", &error);

    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

static void ufo_filter_fft_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterFFTPrivate *priv = UFO_FILTER_FFT_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    int err = CL_SUCCESS;
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);
    guint num_dims = 0, width, height;
    guint *dim_size = NULL;
    ufo_buffer_get_dimensions(input, &num_dims, &dim_size);
    width = dim_size[0];
    height = dim_size[1];

    /* Create FFT plan with appropriate size */
    priv->fft_size.x = pow2round(width);
    if (priv->fft_dimensions == clFFT_2D)
        priv->fft_size.y = pow2round(height);

    clFFT_Plan fft_plan = clFFT_CreatePlan(
                (cl_context) ufo_resource_manager_get_context(manager),
                priv->fft_size, priv->fft_dimensions,
                clFFT_InterleavedComplexFormat, &err);

    dim_size[0] = 2 * priv->fft_size.x;
    dim_size[1] = priv->fft_dimensions == clFFT_1D ? height : priv->fft_size.y;
    ufo_channel_allocate_output_buffers(output_channel, num_dims, dim_size);

    while (input != NULL) {
        UfoBuffer *fft_buffer = ufo_channel_get_output_buffer(output_channel);

        cl_mem fft_buffer_mem = (cl_mem) ufo_buffer_get_device_array(fft_buffer, command_queue);
        cl_mem sinogram_mem = (cl_mem) ufo_buffer_get_device_array(input, command_queue);
        cl_event event, wait_on_event;
        size_t global_work_size[2];

        /* Spread data for interleaved FFT */
        global_work_size[0] = priv->fft_size.x;
        global_work_size[1] = priv->fft_dimensions == clFFT_1D ? height : priv->fft_size.y;
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), (void *) &fft_buffer_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), (void *) &sinogram_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 2, sizeof(int), &width));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 3, sizeof(int), &height));
        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue,
                priv->kernel, 
                2, NULL, global_work_size, NULL, 
                0, NULL, &event));
        
        /* FIXME: we should wait for previous computations */
        CHECK_OPENCL_ERROR(clWaitForEvents(1, &event));
        if (priv->fft_dimensions == clFFT_1D)
            clFFT_ExecuteInterleaved(command_queue,
                fft_plan, (cl_int) height, clFFT_Forward, 
                fft_buffer_mem, fft_buffer_mem,
                1, &wait_on_event, &event);
        else
            clFFT_ExecuteInterleaved(command_queue,
                fft_plan, 1, clFFT_Forward, 
                fft_buffer_mem, fft_buffer_mem,
                1, &wait_on_event, &event);

        /* XXX: FFT execution does _not_ return event */
        /*ufo_filter_account_gpu_time(filter, (void **) &event);*/
        CHECK_OPENCL_ERROR(clFinish(command_queue));

        ufo_buffer_transfer_id(input, fft_buffer);

        ufo_channel_finalize_input_buffer(input_channel, input);
        ufo_channel_finalize_output_buffer(output_channel, fft_buffer);
        input = ufo_channel_get_input_buffer(input_channel);
    }

    ufo_channel_finish(output_channel);
    clFFT_DestroyPlan(fft_plan);
    g_free(dim_size);
}

static void ufo_filter_fft_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterFFT *self = UFO_FILTER_FFT(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_DIMENSIONS:
            switch(g_value_get_uint(value)) {
                case 1:
                    self->priv->fft_dimensions = clFFT_1D;
                    break;
                case 2:
                    self->priv->fft_dimensions = clFFT_2D;
                    break;
                case 3:
                    self->priv->fft_dimensions = clFFT_3D;
                    break;
            }
            break;
        case PROP_SIZE_X:
            self->priv->fft_size.x = g_value_get_uint(value);
            break;
        case PROP_SIZE_Y:
            self->priv->fft_size.y = g_value_get_uint(value);
            break;
        case PROP_SIZE_Z:
            self->priv->fft_size.z = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_fft_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterFFT *self = UFO_FILTER_FFT(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_DIMENSIONS:
            switch (self->priv->fft_dimensions) {
                case clFFT_1D:
                    g_value_set_uint(value, 1);
                    break;
                case clFFT_2D:
                    g_value_set_uint(value, 2);
                    break;
                case clFFT_3D:
                    g_value_set_uint(value, 3);
                    break;
            }
            break;
        case PROP_SIZE_X:
            g_value_set_uint(value, self->priv->fft_size.x);
            break;
        case PROP_SIZE_Y:
            g_value_set_uint(value, self->priv->fft_size.y);
            break;
        case PROP_SIZE_Z:
            g_value_set_uint(value, self->priv->fft_size.z);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_fft_class_init(UfoFilterFFTClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_fft_set_property;
    gobject_class->get_property = ufo_filter_fft_get_property;
    filter_class->initialize = ufo_filter_fft_initialize;
    filter_class->process = ufo_filter_fft_process;

    /* install properties */
    fft_properties[PROP_DIMENSIONS] = 
        g_param_spec_uint("dimensions",
            "Number of FFT dimensions from 1 to 3",
            "Number of FFT dimensions from 1 to 3",
            1, 3, 1,
            G_PARAM_READWRITE);

    fft_properties[PROP_SIZE_X] = 
        g_param_spec_uint("size-x",
            "Size of the FFT transform in x-direction",
            "Size of the FFT transform in x-direction",
            1, 8192, 1,
            G_PARAM_READWRITE);

    fft_properties[PROP_SIZE_Y] = 
        g_param_spec_uint("size-y",
            "Size of the FFT transform in y-direction",
            "Size of the FFT transform in y-direction",
            1, 8192, 1,
            G_PARAM_READWRITE);

    fft_properties[PROP_SIZE_Z] = 
        g_param_spec_uint("size-z",
            "Size of the FFT transform in z-direction",
            "Size of the FFT transform in z-direction",
            1, 8192, 1,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_DIMENSIONS, fft_properties[PROP_DIMENSIONS]);
    g_object_class_install_property(gobject_class, PROP_SIZE_X, fft_properties[PROP_SIZE_X]);
    g_object_class_install_property(gobject_class, PROP_SIZE_Y, fft_properties[PROP_SIZE_Y]);
    g_object_class_install_property(gobject_class, PROP_SIZE_Z, fft_properties[PROP_SIZE_Z]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterFFTPrivate));
}

static void ufo_filter_fft_init(UfoFilterFFT *self)
{
    UfoFilterFFTPrivate *priv = self->priv = UFO_FILTER_FFT_GET_PRIVATE(self);
    priv->fft_dimensions = clFFT_1D;
    priv->fft_size.x = 1;
    priv->fft_size.y = 1;
    priv->fft_size.z = 1;
    priv->kernel = NULL;

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output0", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_FFT, NULL);
}
