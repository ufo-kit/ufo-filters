#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-forward-project.h"

/**
 * SECTION:ufo-filter-forward-project
 * @Short_description: Forward project slices
 * @Title: forwardproject
 *
 * Forward project slice data to simulate a parallel-beam detector. The output
 * is a sinogram with projections taken at angles space
 * #UfoFilterForwardProject:angle-step units apart.
 */

struct _UfoFilterForwardProjectPrivate {
    cl_kernel kernel;
    gfloat angle_step;
    guint num_projections;
};

G_DEFINE_TYPE(UfoFilterForwardProject, ufo_filter_forward_project, UFO_TYPE_FILTER)

#define UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_FORWARD_PROJECT, UfoFilterForwardProjectPrivate))

enum {
    PROP_0,
    PROP_ANGLE_STEP,
    PROP_NUM_PROJECTIONS,
    N_PROPERTIES
};

static GParamSpec *forward_project_properties[N_PROPERTIES] = { NULL, };

static void ufo_filter_forward_project_initialize(UfoFilter *filter)
{
    UfoFilterForwardProject *self = UFO_FILTER_FORWARD_PROJECT(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->kernel = ufo_resource_manager_get_kernel(manager, "forwardproject.cl","forwardproject", NULL);

    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}


static void ufo_filter_forward_project_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterForwardProjectPrivate *priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();

    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);

    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);
    UfoBuffer *output = NULL;

    guint num_dims = 0;
    guint *dim_size = NULL;
    ufo_buffer_get_dimensions(input, &num_dims, &dim_size);

    cl_int errcode = CL_SUCCESS;
    cl_kernel kernel = priv->kernel;
    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);
    cl_image_format image_format = { .image_channel_order = CL_R, .image_channel_data_type = CL_FLOAT };

    cl_mem slice = clCreateImage2D(context,
                                   CL_MEM_READ_ONLY, &image_format, dim_size[0], dim_size[1],
                                   0, NULL, &errcode);

    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &slice));
    CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 2, sizeof(float), &priv->angle_step));

    const gsize src_origin[3] = { 0, 0, 0 };
    const gsize region[3] = { dim_size[0], dim_size[1], 1 };

    dim_size[1] = priv->num_projections;
    ufo_channel_allocate_output_buffers(output_channel, 2, dim_size);
    const gsize global_work_size[] = { dim_size[0], dim_size[1] }; 

    while (input != NULL) {
        cl_mem input_mem = (cl_mem) ufo_buffer_get_device_array(input, command_queue);
        CHECK_OPENCL_ERROR(clEnqueueCopyBufferToImage(command_queue,
                                               input_mem, slice,
                                               0, src_origin, region,
                                               0, NULL, NULL));

        output = ufo_channel_get_output_buffer(output_channel);
        cl_mem output_mem = (cl_mem) ufo_buffer_get_device_array(output, command_queue);
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &output_mem));
        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue, kernel,
                                           2, NULL, global_work_size, NULL,
                                           0, NULL, NULL));

        ufo_channel_finalize_input_buffer(input_channel, input);
        ufo_channel_finalize_output_buffer(output_channel, output);
        input = ufo_channel_get_input_buffer(input_channel);
    }

    ufo_channel_finish(output_channel);
    clReleaseMemObject(slice);
    g_free(dim_size);
}

static void ufo_filter_forward_project_set_property(GObject *object,
        guint           property_id,
        const GValue    *value,
        GParamSpec      *pspec)
{
    UfoFilterForwardProjectPrivate *priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_ANGLE_STEP:
            priv->angle_step = g_value_get_float(value);
            break;
        case PROP_NUM_PROJECTIONS:
            priv->num_projections = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_forward_project_get_property(GObject *object,
        guint       property_id,
        GValue      *value,
        GParamSpec  *pspec)
{
    UfoFilterForwardProjectPrivate *priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_ANGLE_STEP:
            g_value_set_float(value, priv->angle_step);
            break;
        case PROP_NUM_PROJECTIONS:
            g_value_set_uint(value, priv->num_projections);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_forward_project_class_init(UfoFilterForwardProjectClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);
    gobject_class->set_property = ufo_filter_forward_project_set_property;
    gobject_class->get_property = ufo_filter_forward_project_get_property;
    filter_class->initialize = ufo_filter_forward_project_initialize;
    filter_class->process = ufo_filter_forward_project_process;

    forward_project_properties[PROP_ANGLE_STEP] =
        g_param_spec_float("angle-step",
                           "Increment of angle in radians",
                           "Increment of angle in radians",
                           -4.0f * ((gfloat) G_PI), 
                           +4.0f * ((gfloat) G_PI), 
                           0.0f,
                           G_PARAM_READWRITE);

    forward_project_properties[PROP_NUM_PROJECTIONS] =
        g_param_spec_uint("num-projections",
                          "Number of projections",
                          "Number of projections",
                          1, 8192, 256,
                          G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_ANGLE_STEP, forward_project_properties[PROP_ANGLE_STEP]);
    g_object_class_install_property(gobject_class, PROP_NUM_PROJECTIONS, forward_project_properties[PROP_NUM_PROJECTIONS]);
    g_type_class_add_private(gobject_class, sizeof(UfoFilterForwardProjectPrivate));
}

static void ufo_filter_forward_project_init(UfoFilterForwardProject *self)
{
    self->priv = UFO_FILTER_FORWARD_PROJECT_GET_PRIVATE(self);

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output0", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_FORWARD_PROJECT, NULL);
}
