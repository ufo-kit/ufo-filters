#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-cl.h"

/**
 * SECTION:ufo-filter-cl
 * @Short_description: Execute arbitrary OpenCL kernels
 * @Title: cl
 *
 * Execute an OpenCL kernel specified by #UfoFilterCl:kernel and load from
 * #UfoFilterCl:file on two-dimensional input.
 */

struct _UfoFilterClPrivate {
    cl_kernel kernel;
    gchar *file_name;
    gchar *kernel_name;
    gboolean combine;
    gint static_argument;
};

G_DEFINE_TYPE(UfoFilterCl, ufo_filter_cl, UFO_TYPE_FILTER)

#define UFO_FILTER_CL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_CL, UfoFilterClPrivate))

enum {
    PROP_0,
    PROP_FILE_NAME,
    PROP_KERNEL,
    PROP_COMBINE,
    PROP_STATIC_ARGUMENT,
    N_PROPERTIES
};

static GParamSpec *cl_properties[N_PROPERTIES] = { NULL, };

static void ufo_filter_cl_initialize(UfoFilter *filter)
{
}

static void process_regular(UfoFilter *self,
        UfoFilterClPrivate *priv, 
        cl_command_queue command_queue, 
        cl_kernel kernel)
{
    UfoChannel *input_channel = ufo_filter_get_input_channel(self);
    UfoChannel *output_channel = ufo_filter_get_output_channel(self);
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);

    cl_event event;
    guint num_dims = 0;
    guint *dim_size = NULL;
    ufo_buffer_get_dimensions(input, &num_dims, &dim_size);
    ufo_channel_allocate_output_buffers(output_channel, num_dims, dim_size);

    /* We should reject anything that is not 2-dimensional */
    size_t global_work_size[2] = { (size_t) dim_size[0], (size_t) dim_size[1] };

    while (input != NULL) { 
        UfoBuffer *output = ufo_channel_get_output_buffer(output_channel);
        cl_mem frame_mem = (cl_mem) ufo_buffer_get_device_array(input, command_queue);
        cl_mem result_mem = (cl_mem) ufo_buffer_get_device_array(output, command_queue);

        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &frame_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &result_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 2, sizeof(float)*16*16, NULL));

        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue,
            kernel,
            2, NULL, global_work_size, NULL,
            0, NULL, &event));

        ufo_buffer_attach_event(output, event);
        ufo_channel_finalize_input_buffer(input_channel, input);
        ufo_channel_finalize_output_buffer(output_channel, output);
        input = ufo_channel_get_input_buffer(input_channel);
    }

    ufo_channel_finish(output_channel);
    g_free(dim_size);
}

static void process_combine(UfoFilter *self,
        UfoFilterClPrivate *priv, 
        cl_command_queue command_queue, 
        cl_kernel kernel)
{
    UfoChannel *output_channel = ufo_filter_get_output_channel(self);
    
    UfoChannel *input_a = ufo_filter_get_input_channel_by_name(self, "input0");
    UfoChannel *input_b = ufo_filter_get_input_channel_by_name(self, "input1");

    size_t local_work_size[2] = { 16, 16 };
    guint num_dims;
    guint *dim_size = NULL;

    UfoBuffer *a = ufo_channel_get_input_buffer(input_a);
    UfoBuffer *b = ufo_channel_get_input_buffer(input_b);
    ufo_buffer_get_dimensions(a, &num_dims, &dim_size);
    ufo_channel_allocate_output_buffers(output_channel, num_dims, dim_size);

    size_t global_work_size[2];
    global_work_size[0] = (size_t) dim_size[0];
    global_work_size[1] = (size_t) dim_size[1];
    cl_event event;

    while ((a != NULL) && (b != NULL)) {
        UfoBuffer *output = ufo_channel_get_output_buffer(output_channel);
        cl_mem a_mem = (cl_mem) ufo_buffer_get_device_array(a, command_queue);
        cl_mem b_mem = (cl_mem) ufo_buffer_get_device_array(b, command_queue);
        cl_mem result_mem = (cl_mem) ufo_buffer_get_device_array(output, command_queue);

        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &a_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &b_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *) &result_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(kernel, 3, sizeof(float)*local_work_size[0]*local_work_size[1], NULL));

        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue,
            kernel,
            2, NULL, global_work_size, NULL,
            0, NULL, &event));

        switch (priv->static_argument) {
            case 0:
                ufo_channel_finalize_input_buffer(input_a, a);
                ufo_channel_finalize_input_buffer(input_b, b);
                a = ufo_channel_get_input_buffer(input_a);
                b = ufo_channel_get_input_buffer(input_b);
                break;
            case 1:
                ufo_channel_finalize_input_buffer(input_b, b);
                b = ufo_channel_get_input_buffer(input_b);
                break;
            case 2:
                ufo_channel_finalize_input_buffer(input_a, a);
                a = ufo_channel_get_input_buffer(input_a);
                break;
        }
        
        /* ufo_buffer_attach_event(output, event); */
        ufo_channel_finalize_output_buffer(output_channel, output);
    }

    ufo_channel_finish(output_channel);
    g_free(dim_size);
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_cl_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterClPrivate *priv = UFO_FILTER_CL_GET_PRIVATE(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);

    UfoResourceManager *manager = ufo_resource_manager();
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);
    GError *error = NULL;

    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
        return;
    }

    cl_kernel kernel = ufo_resource_manager_get_kernel(manager, priv->file_name, priv->kernel_name, &error);

    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
    
    if (!kernel) {
        ufo_channel_finish(output_channel);
        return;
    }

    if (priv->combine)
        process_combine(filter, priv, command_queue, kernel);
    else
        process_regular(filter, priv, command_queue, kernel);
}

static void ufo_filter_cl_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterClPrivate *priv = UFO_FILTER_CL_GET_PRIVATE(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_FILE_NAME:
            g_free(priv->file_name);
            priv->file_name = g_strdup(g_value_get_string(value));
            break;
        case PROP_KERNEL:
            g_free(priv->kernel_name);
            priv->kernel_name = g_strdup(g_value_get_string(value));
            break;
        case PROP_COMBINE:
            priv->combine = g_value_get_boolean(value);
            break;
        case PROP_STATIC_ARGUMENT:
            priv->static_argument= g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_cl_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterClPrivate *priv = UFO_FILTER_CL_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_FILE_NAME:
            g_value_set_string(value, priv->file_name);
            break;
        case PROP_KERNEL:
            g_value_set_string(value, priv->kernel_name);
            break;
        case PROP_COMBINE:
            g_value_set_boolean(value, priv->combine);
            break;
        case PROP_STATIC_ARGUMENT:
            g_value_set_int(value, priv->static_argument);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_cl_class_init(UfoFilterClClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_cl_set_property;
    gobject_class->get_property = ufo_filter_cl_get_property;
    filter_class->initialize = ufo_filter_cl_initialize;
    filter_class->process = ufo_filter_cl_process;

    cl_properties[PROP_FILE_NAME] = 
        g_param_spec_string("file",
            "File in which the kernel resides",
            "File in which the kernel resides",
            "",
            G_PARAM_READWRITE);

    cl_properties[PROP_KERNEL] = 
        g_param_spec_string("kernel",
            "Kernel name",
            "Kernel name",
            "",
            G_PARAM_READWRITE);

    cl_properties[PROP_COMBINE] = 
        g_param_spec_boolean("combine",
            "Use two frames as an input for a function",
            "Use two frames as an input for a function",
            FALSE,
            G_PARAM_READWRITE);

    cl_properties[PROP_STATIC_ARGUMENT] = 
        g_param_spec_int("static-argument",
            "Input of channel k is used for each iteration",
            "Input of channel k is used for each iteration",
            0, 2, 2,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_FILE_NAME, cl_properties[PROP_FILE_NAME]);
    g_object_class_install_property(gobject_class, PROP_KERNEL, cl_properties[PROP_KERNEL]);
    g_object_class_install_property(gobject_class, PROP_COMBINE, cl_properties[PROP_COMBINE]);
    g_object_class_install_property(gobject_class, PROP_STATIC_ARGUMENT, cl_properties[PROP_STATIC_ARGUMENT]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterClPrivate));
}

static void ufo_filter_cl_init(UfoFilterCl *self)
{
    UfoFilterClPrivate *priv = self->priv = UFO_FILTER_CL_GET_PRIVATE(self);
    priv->file_name = NULL;
    priv->kernel_name = NULL;
    priv->kernel = NULL;
    priv->static_argument = 0;

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_input(UFO_FILTER(self), "input1", 2);
    ufo_filter_register_output(UFO_FILTER(self), "image", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_CL, NULL);
}
