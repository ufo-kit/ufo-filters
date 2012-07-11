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
    cl_kernel   kernel;
    gchar      *file_name;
    gchar      *kernel_name;
    gboolean    combine;
    gint        static_argument;
    gsize       global_work_size[2];
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


static UfoEventList *
process_regular(UfoFilter *filter, UfoBuffer *inputs[], UfoBuffer *outputs[], gpointer cmd_queue, GError **error)
{
    UfoFilterClPrivate *priv = UFO_FILTER_CL_GET_PRIVATE(filter);
    UfoEventList *event_list = ufo_event_list_new (1);
    cl_event *events = ufo_event_list_get_event_array (event_list);
    cl_mem a_mem = (cl_mem) ufo_buffer_get_device_array(inputs[0], (cl_command_queue) cmd_queue);
    cl_mem result_mem = (cl_mem) ufo_buffer_get_device_array(outputs[0], (cl_command_queue) cmd_queue);

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), (void *) &a_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), (void *) &result_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 2, sizeof(float)*16*16, NULL));

    CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel((cl_command_queue) cmd_queue,
                priv->kernel,
                2, NULL, priv->global_work_size, NULL,
                0, NULL, &events[0]));

    return event_list;
}

static UfoEventList *
process_combine(UfoFilter *filter, UfoBuffer *inputs[], UfoBuffer *outputs[], gpointer cmd_queue, GError **error)
{
    UfoFilterClPrivate *priv = UFO_FILTER_CL_GET_PRIVATE(filter);
    UfoEventList *event_list = ufo_event_list_new (1);
    cl_event *events = ufo_event_list_get_event_array (event_list);
    cl_mem a_mem = (cl_mem) ufo_buffer_get_device_array(inputs[0], (cl_command_queue) cmd_queue);
    cl_mem b_mem = (cl_mem) ufo_buffer_get_device_array(inputs[1], (cl_command_queue) cmd_queue);
    cl_mem result_mem = (cl_mem) ufo_buffer_get_device_array(outputs[0], (cl_command_queue) cmd_queue);

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), (void *) &a_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), (void *) &b_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 2, sizeof(cl_mem), (void *) &result_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 3, sizeof(float)*16*16, NULL));

    CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel((cl_command_queue) cmd_queue,
        priv->kernel,
        2, NULL, priv->global_work_size, NULL,
        0, NULL, &events[0]));

    return event_list;
}

static void
ufo_filter_cl_initialize(UfoFilter *filter, UfoBuffer *inputs[], guint **dims, GError **error)
{
    UfoFilterClPrivate *priv = UFO_FILTER_CL_GET_PRIVATE(filter);
    UfoFilterClass *filter_class = UFO_FILTER_GET_CLASS(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    guint width, height;

    ufo_buffer_get_2d_dimensions(inputs[0], &width, &height);
    priv->global_work_size[0] = dims[0][0] = width;
    priv->global_work_size[1] = dims[0][1] = height;
    priv->kernel = ufo_resource_manager_get_kernel(manager, priv->file_name, priv->kernel_name, error);

    if (priv->combine)
        filter_class->process_gpu = process_combine;
}

static void
ufo_filter_cl_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterClPrivate *priv = UFO_FILTER_CL_GET_PRIVATE(object);

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

static void
ufo_filter_cl_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
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

static void
ufo_filter_cl_class_init(UfoFilterClClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_cl_set_property;
    gobject_class->get_property = ufo_filter_cl_get_property;
    filter_class->process_gpu = process_regular;
    filter_class->initialize = ufo_filter_cl_initialize;

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

static void
ufo_filter_cl_init(UfoFilterCl *self)
{
    UfoFilterClPrivate *priv = self->priv = UFO_FILTER_CL_GET_PRIVATE(self);
    UfoInputParameter input_params[] = {{2, UFO_FILTER_INFINITE_INPUT}};
    UfoOutputParameter output_params[] = {{2}};

    priv->file_name = NULL;
    priv->kernel_name = NULL;
    priv->kernel = NULL;
    priv->static_argument = 0;

    ufo_filter_register_inputs (UFO_FILTER (self), 1, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_CL, NULL);
}
