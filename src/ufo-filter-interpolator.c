#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-interpolator.h"

/**
 * SECTION:ufo-filter-interpolator
 * @Short_description: Interpolate between two images
 * @Title: interpolator
 *
 * This node reads exactly one two-dimensional image from each of its two inputs
 * "input0" and "input1". Then it outputs #UfoFilterInterpolator:num-steps
 * frames that are the result of a linear interpolation (blended with a*i1 +
 * (1-a)*i2, 0 <= a <= 1) between those two input images.
 */

struct _UfoFilterInterpolatorPrivate {
    cl_kernel   kernel;
    guint       num_steps;
    guint       current_step;
    size_t      global_work_size[2];
};

G_DEFINE_TYPE(UfoFilterInterpolator, ufo_filter_interpolator, UFO_TYPE_FILTER)

#define UFO_FILTER_INTERPOLATOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_INTERPOLATOR, UfoFilterInterpolatorPrivate))

enum {
    PROP_0,
    PROP_STEPS,
    N_PROPERTIES
};

static GParamSpec *interpolator_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_interpolator_initialize(UfoFilter *filter, UfoBuffer *params[], guint **dims, GError **error)
{
    UfoFilterInterpolatorPrivate *priv = UFO_FILTER_INTERPOLATOR_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_filter_get_resource_manager(filter);
    guint width_a, width_b;
    guint height_a, height_b;
    priv->kernel = ufo_resource_manager_get_kernel(manager, "interpolator.cl", "interpolate", error);

    ufo_buffer_get_2d_dimensions(params[0], &width_a, &height_a);
    ufo_buffer_get_2d_dimensions(params[1], &width_b, &height_b);

    /* TODO: make proper error */
    g_assert (width_a == width_b);
    g_assert (height_a == height_b);

    dims[0][0] = width_a;
    dims[0][1] = height_a;

    priv->global_work_size[0] = (size_t) width_a;
    priv->global_work_size[1] = (size_t) height_a;
    priv->current_step = 0;
}

static void
ufo_filter_interpolator_process_gpu (UfoFilter *filter, UfoBuffer *input[], UfoBuffer *output[], gpointer cmd_queue, GError **error)
{
    UfoFilterInterpolatorPrivate *priv = UFO_FILTER_INTERPOLATOR_GET_PRIVATE(filter);

    cl_command_queue command_queue = (cl_command_queue) cmd_queue;
    cl_mem a_mem = (cl_mem) ufo_buffer_get_device_array(input[0], command_queue);
    cl_mem b_mem = (cl_mem) ufo_buffer_get_device_array(input[1], command_queue);
    cl_mem result_mem = (cl_mem) ufo_buffer_get_device_array(output[0], command_queue);
    cl_kernel kernel = priv->kernel;

    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &a_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &b_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *) &result_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 3, sizeof(cl_int), &priv->current_step));
    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 4, sizeof(cl_int), &priv->num_steps))

    ufo_profiler_call (ufo_filter_get_profiler (filter),
                       cmd_queue, priv->kernel,
                       2, priv->global_work_size, NULL);

}

static void ufo_filter_interpolator_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterInterpolatorPrivate *priv = UFO_FILTER_INTERPOLATOR_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_STEPS:
            priv->num_steps = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_interpolator_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterInterpolatorPrivate *priv = UFO_FILTER_INTERPOLATOR_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_STEPS:
            g_value_set_uint(value, priv->num_steps);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_interpolator_class_init(UfoFilterInterpolatorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_interpolator_set_property;
    gobject_class->get_property = ufo_filter_interpolator_get_property;
    filter_class->initialize = ufo_filter_interpolator_initialize;
    filter_class->process_gpu = ufo_filter_interpolator_process_gpu;

    /* install properties */
    interpolator_properties[PROP_STEPS] =
        g_param_spec_uint("num-steps",
            "Number of steps to interpolate between",
            "Number of steps to interpolate between",
            1, 8192, 2,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_STEPS, interpolator_properties[PROP_STEPS]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterInterpolatorPrivate));
}

static void ufo_filter_interpolator_init(UfoFilterInterpolator *self)
{
    UfoFilterInterpolatorPrivate *priv = self->priv = UFO_FILTER_INTERPOLATOR_GET_PRIVATE(self);
    UfoInputParameter input_params[] = {
        {2, 1},
        {2, 1}};
    UfoOutputParameter output_params[] = {{2}};

    priv->kernel = NULL;
    priv->num_steps = 2;

    ufo_filter_register_inputs (UFO_FILTER(self), 2, input_params);
    ufo_filter_register_outputs (UFO_FILTER(self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_INTERPOLATOR, NULL);
}
