#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "config.h"

#ifdef HAVE_OCLFFT
#include <clFFT.h>
#endif

#ifdef HAVE_FFTW3
#include <fftw3.h>
#endif

#include <string.h>
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
    guint width, height;
    enum {
        FFT_1D = 1,
        FFT_2D,
        FFT_3D
    } fft_dimensions;

#ifdef HAVE_OCLFFT
    cl_kernel   kernel;
    clFFT_Plan  cl_fft_plan;
    clFFT_Dim3  fft_size;
    gsize       global_work_size[2];
#endif
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

static guint32
pow2round(guint32 x)
{
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x+1;
}

static GError *
ufo_filter_fft_initialize(UfoFilter *filter, UfoBuffer *params[], guint **dims)
{
    UfoFilterFFTPrivate *priv = UFO_FILTER_FFT_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();

#ifdef HAVE_OCLFFT
    cl_int err = CL_SUCCESS;
    GError *error = NULL;
    priv->kernel = ufo_resource_manager_get_kernel(manager, "fft.cl", "fft_spread", &error);

    if (error != NULL)
        return error;

    ufo_buffer_get_2d_dimensions(params[0], &priv->width, &priv->height);
    priv->fft_size.x = pow2round(priv->width);
    clFFT_Dimension cl_fft_dimensions;

    switch (priv->fft_dimensions) {
        case FFT_1D:
            cl_fft_dimensions = clFFT_1D;
            break;
        case FFT_2D:
            priv->fft_size.y = pow2round(priv->height);
            cl_fft_dimensions = clFFT_2D;
            break;
        case FFT_3D:
            cl_fft_dimensions = clFFT_3D;
            break;
    }

    priv->cl_fft_plan = clFFT_CreatePlan(
            (cl_context) ufo_resource_manager_get_context(manager),
            priv->fft_size, cl_fft_dimensions,
            clFFT_InterleavedComplexFormat, &err);

    dims[0][0] = 2 * priv->fft_size.x;
    dims[0][1] = priv->fft_dimensions == FFT_1D ? priv->height : priv->fft_size.y;

    priv->global_work_size[0] = priv->fft_size.x;
    priv->global_work_size[1] = dims[0][1];
#endif

    return NULL;
}

#ifdef HAVE_OCLFFT
static GError *
ufo_filter_fft_process_gpu(UfoFilter *filter, UfoBuffer *params[], UfoBuffer *results[], gpointer cmd_queue)
{
    UfoFilterFFTPrivate *priv = UFO_FILTER_FFT_GET_PRIVATE(filter);
    cl_mem fft_buffer_mem = (cl_mem) ufo_buffer_get_device_array(results[0], (cl_command_queue) cmd_queue);
    cl_mem sinogram_mem = (cl_mem) ufo_buffer_get_device_array(params[0], (cl_command_queue) cmd_queue);
    cl_event event, wait_on_event;

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), (void *) &fft_buffer_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), (void *) &sinogram_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 2, sizeof(int), &priv->width));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 3, sizeof(int), &priv->height));
    CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel((cl_command_queue) cmd_queue,
                priv->kernel, 
                2, NULL, priv->global_work_size, NULL, 
                0, NULL, &event));

    /* FIXME: we should wait for previous computations */
    CHECK_OPENCL_ERROR(clWaitForEvents(1, &event));
    if (priv->fft_dimensions == FFT_1D)
        clFFT_ExecuteInterleaved((cl_command_queue) cmd_queue,
                priv->cl_fft_plan, (cl_int) priv->height, clFFT_Forward, 
                fft_buffer_mem, fft_buffer_mem,
                1, &wait_on_event, &event);
    else
        clFFT_ExecuteInterleaved((cl_command_queue) cmd_queue,
                priv->cl_fft_plan, 1, clFFT_Forward, 
                fft_buffer_mem, fft_buffer_mem,
                1, &wait_on_event, &event);

    /* XXX: FFT execution does _not_ return event */
    CHECK_OPENCL_ERROR(clFinish((cl_command_queue) cmd_queue));
    return NULL;
}
#endif

#ifdef HAVE_FFTW3
static GError *
ufo_filter_fft_process_cpu(UfoFilter *filter, UfoBuffer *params[], UfoBuffer *results[], gpointer cmd_queue)
{
    UfoFilterFFTPrivate *priv = UFO_FILTER_FFT_GET_PRIVATE(filter);

    gulong idist = (gulong) priv->width;
    gulong odist = (gulong) pow2round(priv->width);
    gfloat *in = ufo_buffer_get_host_array(params[0], (cl_command_queue) cmd_queue);
    gfloat *out = ufo_buffer_get_host_array(results[0], (cl_command_queue) cmd_queue);

    fftwf_plan plan = fftwf_plan_many_dft_r2c(1, (gint *) &priv->width, (gint) priv->height,
            in, NULL, 1, (gint) idist,
            out, NULL, 1, (gint) odist,
            0);
    fftwf_execute(plan);
    fftwf_destroy_plan(plan);

    memcpy(out, out + odist * sizeof(gfloat), odist * sizeof(gfloat));
    return NULL;
}
#endif

static void
ufo_filter_fft_finalize(GObject *object)
{
    UfoFilterFFTPrivate *priv = UFO_FILTER_FFT_GET_PRIVATE(object);

#ifdef HAVE_OCLFFT
    clFFT_DestroyPlan(priv->cl_fft_plan);
#endif

    G_OBJECT_CLASS(ufo_filter_fft_parent_class)->finalize(object);
}

static void
ufo_filter_fft_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterFFT *self = UFO_FILTER_FFT(object);

    switch (property_id) {
        case PROP_DIMENSIONS:
            self->priv->fft_dimensions = g_value_get_uint(value);
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

static void
ufo_filter_fft_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterFFT *self = UFO_FILTER_FFT(object);

    switch (property_id) {
        case PROP_DIMENSIONS:
            g_value_set_uint(value, self->priv->fft_dimensions);
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

static void
ufo_filter_fft_class_init(UfoFilterFFTClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_fft_set_property;
    gobject_class->get_property = ufo_filter_fft_get_property;
    gobject_class->finalize = ufo_filter_fft_finalize;
    filter_class->initialize = ufo_filter_fft_initialize;

#ifdef HAVE_OCLFFT
    filter_class->process_gpu = ufo_filter_fft_process_gpu;
#endif

#ifdef HAVE_FFTW3
    filter_class->process_cpu = ufo_filter_fft_process_cpu;
#endif

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

static void
ufo_filter_fft_init(UfoFilterFFT *self)
{
    UfoFilterFFTPrivate *priv = self->priv = UFO_FILTER_FFT_GET_PRIVATE(self);
    priv->fft_dimensions = FFT_1D;
    priv->fft_size.x = 1;
    priv->fft_size.y = 1;
    priv->fft_size.z = 1;
    priv->kernel = NULL;

    ufo_filter_register_inputs (UFO_FILTER (self), 2, NULL);
    ufo_filter_register_outputs (UFO_FILTER (self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_FFT, NULL);
}

