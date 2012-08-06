#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <math.h>
#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-gaussian-blur.h"

/**
 * SECTION:ufo-filter-gaussian-blur
 * @Short_description: Blur image with a Gaussian filter
 * @Title: gaussianblur
 *
 * Detailed description.
 */

struct _UfoFilterGaussianBlurPrivate {
    guint       size;
    gfloat      sigma;
    cl_kernel   h_kernel;
    cl_kernel   v_kernel;
    cl_mem      weights_mem;
    cl_mem      intermediate_mem;
    size_t      global_work_size[2];
};

G_DEFINE_TYPE(UfoFilterGaussianBlur, ufo_filter_gaussian_blur, UFO_TYPE_FILTER)

#define UFO_FILTER_GAUSSIAN_BLUR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_GAUSSIAN_BLUR, UfoFilterGaussianBlurPrivate))

enum {
    PROP_0,
    PROP_SIZE,
    PROP_SIGMA,
    N_PROPERTIES
};

static GParamSpec *gaussian_blur_properties[N_PROPERTIES] = { NULL, };


static void
ufo_filter_gaussian_blur_initialize(UfoFilter *filter, UfoBuffer *params[], guint **dims, GError **error)
{
    UfoFilterGaussianBlurPrivate *priv = UFO_FILTER_GAUSSIAN_BLUR_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_filter_get_resource_manager(filter);
    GError *tmp_error = NULL;
    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    cl_int cl_error = CL_SUCCESS;
    priv->h_kernel = ufo_resource_manager_get_kernel(manager, "gaussian.cl", "h_gaussian", &tmp_error);
    priv->v_kernel = ufo_resource_manager_get_kernel(manager, "gaussian.cl", "v_gaussian", &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error (error, tmp_error);
        return;
    }

    ufo_buffer_get_2d_dimensions(params[0], &dims[0][0], &dims[0][1]);

    const guint kernel_size = priv->size;
    const guint half_kernel_size = kernel_size / 2;
    gfloat *weights = g_malloc0(kernel_size * sizeof(gfloat));
    gfloat weight_sum = 0.0;

    for (guint i = 0; i < half_kernel_size + 1; i++) {
        gfloat x = (gfloat) (half_kernel_size - i);
        weights[i] = (gfloat) (1.0 / (priv->sigma * sqrt(2*G_PI)) * exp((x * x) / (-2.0 * priv->sigma * priv->sigma)));
        weights[kernel_size-i-1] = weights[i];
    }

    for (guint i = 0; i < kernel_size; i++)
        weight_sum += weights[i];

    for (guint i = 0; i < kernel_size; i++)
        weights[i] /= weight_sum;

    priv->weights_mem = clCreateBuffer(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            5 * sizeof(float), weights, &cl_error);

    CHECK_OPENCL_ERROR(cl_error);

    priv->intermediate_mem = clCreateBuffer(context,
            CL_MEM_READ_WRITE, dims[0][0] * dims[0][1] * sizeof(float), NULL, &cl_error);

    CHECK_OPENCL_ERROR(cl_error);

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->h_kernel, 2, sizeof(cl_mem), &priv->weights_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->h_kernel, 3, sizeof(int), &half_kernel_size));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->v_kernel, 2, sizeof(cl_mem), &priv->weights_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->v_kernel, 3, sizeof(int), &half_kernel_size));

    priv->global_work_size[0] = dims[0][0];
    priv->global_work_size[1] = dims[0][1];

    g_free(weights);
}

static UfoEventList *
ufo_filter_gaussian_blur_process_gpu(UfoFilter *filter, UfoBuffer *params[], UfoBuffer *results[], gpointer cmd_queue, GError **error)
{
    UfoFilterGaussianBlurPrivate *priv = UFO_FILTER_GAUSSIAN_BLUR_GET_PRIVATE(filter);
    UfoEventList *event_list = ufo_event_list_new (2);
    cl_event *events = ufo_event_list_get_event_array (event_list);
    cl_mem input_mem = ufo_buffer_get_device_array(params[0], (cl_command_queue) cmd_queue);
    cl_mem output_mem = ufo_buffer_get_device_array(results[0], (cl_command_queue) cmd_queue);

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->h_kernel, 0, sizeof(cl_mem), &input_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->h_kernel, 1, sizeof(cl_mem), &priv->intermediate_mem));

    CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel((cl_command_queue) cmd_queue, priv->h_kernel,
                2, NULL, priv->global_work_size, NULL,
                0, NULL, &events[0]));

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->v_kernel, 0, sizeof(cl_mem), &priv->intermediate_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->v_kernel, 1, sizeof(cl_mem), &output_mem));

    CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel((cl_command_queue) cmd_queue, priv->v_kernel,
                2, NULL, priv->global_work_size, NULL,
                0, NULL, &events[1]));

    return event_list;
}

static void
ufo_filter_gaussian_blur_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterGaussianBlurPrivate *priv = UFO_FILTER_GAUSSIAN_BLUR_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_SIZE:
            priv->size = g_value_get_uint(value);
            break;
        case PROP_SIGMA:
            priv->sigma = g_value_get_float(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_gaussian_blur_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterGaussianBlurPrivate *priv = UFO_FILTER_GAUSSIAN_BLUR_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_SIZE:
            g_value_set_uint(value, priv->size);
            break;
        case PROP_SIGMA:
            g_value_set_float(value, priv->sigma);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_gaussian_blur_finalize (GObject *object)
{
    UfoFilterGaussianBlurPrivate *priv = UFO_FILTER_GAUSSIAN_BLUR_GET_PRIVATE (object);

    CHECK_OPENCL_ERROR (clReleaseMemObject (priv->weights_mem));
    CHECK_OPENCL_ERROR (clReleaseMemObject (priv->intermediate_mem));

    G_OBJECT_CLASS (ufo_filter_gaussian_blur_parent_class)->finalize (object);
}

static void
ufo_filter_gaussian_blur_dispose (GObject *object)
{
    G_OBJECT_CLASS (ufo_filter_gaussian_blur_parent_class)->dispose (object);
}

static void
ufo_filter_gaussian_blur_class_init(UfoFilterGaussianBlurClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_gaussian_blur_set_property;
    gobject_class->get_property = ufo_filter_gaussian_blur_get_property;
    gobject_class->finalize     = ufo_filter_gaussian_blur_finalize;
    gobject_class->dispose      = ufo_filter_gaussian_blur_dispose;
    filter_class->initialize    = ufo_filter_gaussian_blur_initialize;
    filter_class->process_gpu   = ufo_filter_gaussian_blur_process_gpu;

    gaussian_blur_properties[PROP_SIZE] =
        g_param_spec_uint("size",
            "Size of the kernel",
            "Size of the kernel",
            3, 1000, 5,
            G_PARAM_READWRITE);

    gaussian_blur_properties[PROP_SIGMA] =
        g_param_spec_float("sigma",
            "sigma",
            "sigma",
            1.0f, 1000.0f, 1.0f,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_SIZE, gaussian_blur_properties[PROP_SIZE]);
    g_object_class_install_property(gobject_class, PROP_SIGMA, gaussian_blur_properties[PROP_SIGMA]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterGaussianBlurPrivate));
}

static void
ufo_filter_gaussian_blur_init(UfoFilterGaussianBlur *self)
{
    UfoFilterGaussianBlurPrivate *priv = self->priv = UFO_FILTER_GAUSSIAN_BLUR_GET_PRIVATE(self);
    UfoInputParameter input_params[] = {{2, UFO_FILTER_INFINITE_INPUT}};
    UfoOutputParameter output_params[] = {{2}};

    priv->size = 5;
    priv->sigma = 1.0f;

    ufo_filter_register_inputs (UFO_FILTER (self), 1, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_GAUSSIAN_BLUR, NULL);
}
