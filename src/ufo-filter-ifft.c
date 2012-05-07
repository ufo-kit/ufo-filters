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

#include "ufo-filter-ifft.h"

/**
 * SECTION:ufo-filter-ifft
 * @Short_description: Compute inverse discrete Fourier transform
 * @Title: ifft
 *
 * Compute inverse discrete Fourier transform using Apples OpenCL FFT library
 * that is provides as liboclfft.
 */

struct _UfoFilterIFFTPrivate {
    cl_kernel pack_kernel;
    cl_kernel normalize_kernel;
    clFFT_Dimension ifft_dimensions;
    clFFT_Dim3 ifft_size;
    guint final_width;
    guint final_height;
};

G_DEFINE_TYPE(UfoFilterIFFT, ufo_filter_ifft, UFO_TYPE_FILTER)

#define UFO_FILTER_IFFT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_IFFT, UfoFilterIFFTPrivate))

enum {
    PROP_0,
    PROP_DIMENSIONS,
    PROP_SIZE_X,
    PROP_SIZE_Y,
    PROP_SIZE_Z,
    PROP_FINAL_WIDTH,
    PROP_FINAL_HEIGHT,
    N_PROPERTIES
};

static GParamSpec *ifft_properties[N_PROPERTIES] = { NULL, };


static void ufo_filter_ifft_initialize(UfoFilter *filter, UfoBuffer *params[])
{
    UfoFilterIFFT *self = UFO_FILTER_IFFT(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->pack_kernel = ufo_resource_manager_get_kernel(manager, "fft.cl", "fft_pack", &error);
    self->priv->normalize_kernel = ufo_resource_manager_get_kernel(manager, "fft.cl","fft_normalize", &error);

    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_ifft_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterIFFTPrivate *priv = UFO_FILTER_IFFT_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    int err = CL_SUCCESS;
    clFFT_Plan ifft_plan = NULL;
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);

    guint num_dims = 0;
    guint *dim_size = NULL;
    ufo_buffer_get_dimensions(input, &num_dims, &dim_size);
    guint width = dim_size[0];
    guint height = dim_size[1];

    if (priv->ifft_size.x != width / 2) {
        priv->ifft_size.x = width / 2;
        if (priv->ifft_dimensions == clFFT_2D)
            priv->ifft_size.y = height;
        clFFT_DestroyPlan(ifft_plan);
        ifft_plan = NULL;
    }

    if (ifft_plan == NULL) {
        ifft_plan = clFFT_CreatePlan(
            (cl_context) ufo_resource_manager_get_context(manager),
            priv->ifft_size, priv->ifft_dimensions,
            clFFT_InterleavedComplexFormat, &err);
    }

    width = (priv->final_width == 0) ? priv->ifft_size.x : priv->final_width;
    height = (priv->final_height == 0) ? height : priv->final_height;
    dim_size[0] = width;
    dim_size[1] = height;
    ufo_channel_allocate_output_buffers(output_channel, num_dims, dim_size);

    const cl_int batch_size = priv->ifft_dimensions == clFFT_1D ? (cl_int) height : 1;

    cl_event *events = NULL;
    cl_uint num_events = 0;
    cl_event event;

    while (input != NULL) {
        cl_mem mem_fft = (cl_mem) ufo_buffer_get_device_array(input, command_queue);

        /* 1. Inverse FFT */
        /* XXX: clFFT_ExecuteInterleaved does not respect given events nor does
         * it return an event object. Therefore, we:
         *
         *  1. wait explicitly on incoming events,
         *  2. we force and wait for command queue termination after enqueuing
         *  the kernel.
         */
        ufo_buffer_get_events(input, (gpointer **) &events, &num_events);
        clWaitForEvents(num_events, events);
        ufo_buffer_clear_events(input);

        clFFT_ExecuteInterleaved(command_queue,
                ifft_plan, batch_size, clFFT_Inverse, 
                mem_fft, mem_fft,
                0, NULL, NULL);
        
        clFinish(command_queue);

        /*ufo_filter_account_gpu_time(filter, &wait_on_event);*/
        /* 2. Pack interleaved complex numbers */
        size_t global_work_size[2];

        width = (priv->final_width == 0) ? priv->ifft_size.x : priv->final_width;
        height = (priv->final_height == 0) ? height : priv->final_height;
        dim_size[0] = width;
        dim_size[1] = height;

        UfoBuffer *result = ufo_channel_get_output_buffer(output_channel);        
        global_work_size[0] = priv->ifft_size.x;
        global_work_size[1] = height;

        float scale = 1.0f / ((float) width);

        if (priv->ifft_dimensions == clFFT_2D)
            scale /= (float) width;

        cl_mem mem_result = (cl_mem) ufo_buffer_get_device_array(result, command_queue);

        clSetKernelArg(priv->pack_kernel, 0, sizeof(cl_mem), (void *) &mem_fft);
        clSetKernelArg(priv->pack_kernel, 1, sizeof(cl_mem), (void *) &mem_result);
        clSetKernelArg(priv->pack_kernel, 2, sizeof(int), &width);
        clSetKernelArg(priv->pack_kernel, 3, sizeof(float), &scale);

        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue,
                priv->pack_kernel,
                2, NULL, global_work_size, NULL,
                0, NULL, &event));

        ufo_buffer_transfer_id(input, result);
        ufo_channel_finalize_input_buffer(input_channel, input);
        ufo_channel_finalize_output_buffer(output_channel, result);
        input = ufo_channel_get_input_buffer(input_channel);
    }
    ufo_channel_finish(output_channel);
    clFFT_DestroyPlan(ifft_plan);
    g_free(dim_size);
}

static void ufo_filter_ifft_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterIFFT *self = UFO_FILTER_IFFT(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_DIMENSIONS:
            switch(g_value_get_uint(value)) {
                case 1:
                    self->priv->ifft_dimensions = clFFT_1D;
                    break;
                case 2:
                    self->priv->ifft_dimensions = clFFT_2D;
                    break;
                case 3:
                    self->priv->ifft_dimensions = clFFT_3D;
                    break;
            }
            break;
        case PROP_SIZE_X:
            self->priv->ifft_size.x = g_value_get_uint(value);
            break;
        case PROP_SIZE_Y:
            self->priv->ifft_size.y = g_value_get_uint(value);
            break;
        case PROP_SIZE_Z:
            self->priv->ifft_size.z = g_value_get_uint(value);
            break;
        case PROP_FINAL_WIDTH:
            self->priv->final_width = g_value_get_uint(value);
            break;
        case PROP_FINAL_HEIGHT:
            self->priv->final_height = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_ifft_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterIFFT *self = UFO_FILTER_IFFT(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_DIMENSIONS:
            switch (self->priv->ifft_dimensions) {
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
            g_value_set_uint(value, self->priv->ifft_size.x);
            break;
        case PROP_SIZE_Y:
            g_value_set_uint(value, self->priv->ifft_size.y);
            break;
        case PROP_SIZE_Z:
            g_value_set_uint(value, self->priv->ifft_size.z);
            break;
        case PROP_FINAL_WIDTH:
            g_value_set_uint(value, self->priv->final_width);
            break;
        case PROP_FINAL_HEIGHT:
            g_value_set_uint(value, self->priv->final_height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_ifft_class_init(UfoFilterIFFTClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_ifft_set_property;
    gobject_class->get_property = ufo_filter_ifft_get_property;
    filter_class->initialize = ufo_filter_ifft_initialize;
    filter_class->process = ufo_filter_ifft_process;

    /* install properties */
    ifft_properties[PROP_DIMENSIONS] = 
        g_param_spec_uint("dimensions",
            "Number of FFT dimensions from 1 to 3",
            "Number of FFT dimensions from 1 to 3",
            1, 3, 1,
            G_PARAM_READWRITE);

    ifft_properties[PROP_SIZE_X] = 
        g_param_spec_int("size-x",
            "Size of the FFT transform in x-direction",
            "Size of the FFT transform in x-direction",
            1, 8192, 1,
            G_PARAM_READWRITE);

    ifft_properties[PROP_SIZE_Y] = 
        g_param_spec_int("size-y",
            "Size of the FFT transform in y-direction",
            "Size of the FFT transform in y-direction",
            1, 8192, 1,
            G_PARAM_READWRITE);

    ifft_properties[PROP_SIZE_Z] = 
        g_param_spec_int("size-z",
            "Size of the FFT transform in z-direction",
            "Size of the FFT transform in z-direction",
            1, 8192, 1,
            G_PARAM_READWRITE);

    ifft_properties[PROP_FINAL_WIDTH] = 
        g_param_spec_uint("final-width",
            "Specify if target width is smaller than FFT size",
            "Specify if target width is smaller than FFT size",
            0, 8192, 0,
            G_PARAM_READWRITE);

    ifft_properties[PROP_FINAL_HEIGHT] = 
        g_param_spec_uint("final-height",
            "Specify if target height is smaller than FFT size",
            "Specify if target height is smaller than FFT size",
            0, 8192, 0,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_DIMENSIONS, ifft_properties[PROP_DIMENSIONS]);
    g_object_class_install_property(gobject_class, PROP_SIZE_X, ifft_properties[PROP_SIZE_X]);
    g_object_class_install_property(gobject_class, PROP_SIZE_Y, ifft_properties[PROP_SIZE_Y]);
    g_object_class_install_property(gobject_class, PROP_SIZE_Z, ifft_properties[PROP_SIZE_Z]);
    g_object_class_install_property(gobject_class, PROP_FINAL_WIDTH, ifft_properties[PROP_FINAL_WIDTH]);
    g_object_class_install_property(gobject_class, PROP_FINAL_HEIGHT, ifft_properties[PROP_FINAL_HEIGHT]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterIFFTPrivate));
}

static void ufo_filter_ifft_init(UfoFilterIFFT *self)
{
    UfoFilterIFFTPrivate *priv = self->priv = UFO_FILTER_IFFT_GET_PRIVATE(self);
    priv->ifft_dimensions = 1;
    priv->ifft_size.x = 1;
    priv->ifft_size.y = 1;
    priv->ifft_size.z = 1;
    priv->final_width = 0;
    priv->final_height = 0;

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output0", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_IFFT, NULL);
}
