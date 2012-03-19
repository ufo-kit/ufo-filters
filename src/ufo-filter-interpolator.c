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
    cl_kernel kernel;
    int num_steps;
};

G_DEFINE_TYPE(UfoFilterInterpolator, ufo_filter_interpolator, UFO_TYPE_FILTER)

#define UFO_FILTER_INTERPOLATOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_INTERPOLATOR, UfoFilterInterpolatorPrivate))

enum {
    PROP_0,
    PROP_STEPS,
    N_PROPERTIES
};

static GParamSpec *interpolator_properties[N_PROPERTIES] = { NULL, };

static void ufo_filter_interpolator_initialize(UfoFilter *filter)
{
    UfoFilterInterpolatorPrivate *priv = UFO_FILTER_INTERPOLATOR_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    priv->kernel = NULL;

    ufo_resource_manager_add_program(manager, "interpolator.cl", NULL, &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
        return;
    }

    priv->kernel = ufo_resource_manager_get_kernel(manager, "interpolate", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_interpolator_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterInterpolatorPrivate *priv = UFO_FILTER_INTERPOLATOR_GET_PRIVATE(filter);

    UfoChannel *input_a = ufo_filter_get_input_channel_by_name(filter, "input0");
    UfoChannel *input_b = ufo_filter_get_input_channel_by_name(filter, "input1");
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    /* We only pop one from each input */
    UfoBuffer *a = ufo_channel_get_input_buffer(input_a);
    UfoBuffer *b = ufo_channel_get_input_buffer(input_b);
    cl_mem a_mem = (cl_mem) ufo_buffer_get_device_array(a, command_queue);
    cl_mem b_mem = (cl_mem) ufo_buffer_get_device_array(b, command_queue);
    cl_kernel kernel = priv->kernel;
    cl_event event;

    guint num_dims = 0;
    guint *dim_size = NULL;
    ufo_buffer_get_dimensions(a, &num_dims, &dim_size);
    ufo_channel_allocate_output_buffers(output_channel, num_dims, dim_size);
    size_t global_work_size[2] = { (size_t) dim_size[0], (size_t) dim_size[1] };

    for (int i = 0; i < priv->num_steps; i++) {
        UfoBuffer *result = ufo_channel_get_output_buffer(output_channel);
        cl_mem result_mem = (cl_mem) ufo_buffer_get_device_array(result, command_queue);

        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &a_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &b_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *) &result_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 3, sizeof(cl_int), &i));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 4, sizeof(cl_int), &priv->num_steps))

        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue,
            priv->kernel,
            2, NULL, global_work_size, NULL,
            0, NULL, &event));

        ufo_buffer_attach_event(result, event);
        ufo_channel_finalize_output_buffer(output_channel, result);
    }
    ufo_channel_finish(output_channel);
    g_free(dim_size);
}

static void ufo_filter_interpolator_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterInterpolatorPrivate *priv = UFO_FILTER_INTERPOLATOR_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_STEPS:
            priv->num_steps = g_value_get_int(value);
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
            g_value_set_int(value, priv->num_steps);
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
    filter_class->process = ufo_filter_interpolator_process;

    /* install properties */
    interpolator_properties[PROP_STEPS] = 
        g_param_spec_int("num-steps",
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
    priv->kernel = NULL;
    priv->num_steps = 2;

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_input(UFO_FILTER(self), "input1", 2);
    ufo_filter_register_output(UFO_FILTER(self), "image", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_INTERPOLATOR, NULL);
}
