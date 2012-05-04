#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-expr.h"
#include "expr-parser.h"

/**
 * SECTION:ufo-filter-expr
 * @Short_description:
 * @Title: expr
 *
 * Detailed description.
 */

struct _UfoFilterExprPrivate {
    gchar *expr;
    cl_kernel kernel;
};

G_DEFINE_TYPE(UfoFilterExpr, ufo_filter_expr, UFO_TYPE_FILTER)

#define UFO_FILTER_EXPR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_EXPR, UfoFilterExprPrivate))

enum {
    PROP_0,
    PROP_EXPRESSION,
    N_PROPERTIES
};

static GParamSpec *expr_properties[N_PROPERTIES] = { NULL, };


static void ufo_filter_expr_initialize(UfoFilter *filter)
{
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_expr_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterExprPrivate *priv = UFO_FILTER_EXPR_GET_PRIVATE(filter);
    UfoChannel *input_channel_x = ufo_filter_get_input_channel_by_name(filter, "input0");
    UfoChannel *input_channel_y = ufo_filter_get_input_channel_by_name(filter, "input1");
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    UfoBuffer *input_x = ufo_channel_get_input_buffer(input_channel_x);
    UfoBuffer *input_y = ufo_channel_get_input_buffer(input_channel_y);
    UfoBuffer *output = NULL;

    if ((input_x == NULL) || (input_y == NULL))
        goto done;

    guint width_x, height_x, width_y, height_y;
    ufo_buffer_get_2d_dimensions(input_x, &width_x, &height_x);
    ufo_buffer_get_2d_dimensions(input_y, &width_y, &height_y);

    if ((width_x != width_y) || (height_x != height_y)) {
        g_warning("Size of both input images must match");
        goto done;
    }

    GError *error = NULL;
    gchar *ocl_kernel_source = parse_expression(priv->expr);
    g_print("%s\n", ocl_kernel_source);
    priv->kernel = ufo_resource_manager_get_kernel_from_source(ufo_resource_manager(),
            ocl_kernel_source, "binary_foo_kernel_2b03c582", &error);
    g_free(ocl_kernel_source);

    if (error != NULL) {
        g_warning("Could not create kernel: %s", error->message);
        g_error_free(error);
        goto done;
    }

    ufo_channel_allocate_output_buffers_like(output_channel, input_x);
    cl_command_queue command_queue = ufo_filter_get_command_queue(filter);
    size_t global_work_size[2] = { width_x, height_x };

    while ((input_x != NULL) && (input_y != NULL)) {
        output = ufo_channel_get_output_buffer(output_channel);

        cl_mem x_mem = ufo_buffer_get_device_array(input_x, command_queue);
        cl_mem y_mem = ufo_buffer_get_device_array(input_y, command_queue);
        cl_mem output_mem = ufo_buffer_get_device_array(output, command_queue);

        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), &x_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), &y_mem));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 2, sizeof(cl_mem), &output_mem));
        CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel(command_queue, priv->kernel,
                    2, NULL, global_work_size, NULL,
                    0, NULL, NULL));

        ufo_channel_finalize_input_buffer(input_channel_x, input_x);
        ufo_channel_finalize_input_buffer(input_channel_y, input_y);
        ufo_channel_finalize_output_buffer(output_channel, output);

        input_x = ufo_channel_get_input_buffer(input_channel_x);
        input_y = ufo_channel_get_input_buffer(input_channel_y);
    }

done:
    ufo_channel_finish(output_channel);
}

static void ufo_filter_expr_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterExprPrivate *priv = UFO_FILTER_EXPR_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_EXPRESSION:
            g_free(priv->expr);
            priv->expr = g_value_dup_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_expr_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterExprPrivate *priv = UFO_FILTER_EXPR_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_EXPRESSION:
            g_value_set_string(value, priv->expr);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_expr_finalize(GObject *object)
{
    UfoFilterExprPrivate *priv = UFO_FILTER_EXPR_GET_PRIVATE(object);
    g_free(priv->expr);
}

static void ufo_filter_expr_class_init(UfoFilterExprClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_expr_set_property;
    gobject_class->get_property = ufo_filter_expr_get_property;
    gobject_class->finalize = ufo_filter_expr_finalize;
    filter_class->initialize = ufo_filter_expr_initialize;
    filter_class->process = ufo_filter_expr_process;
    
    expr_properties[PROP_EXPRESSION] = 
        g_param_spec_string("expression",
            "A mathematical expression",
            "A mathematical expression that combines x and y",
            "x+y",
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property(gobject_class, i, expr_properties[i]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterExprPrivate));
}

static void ufo_filter_expr_init(UfoFilterExpr *self)
{
    UfoFilterExprPrivate *priv = self->priv = UFO_FILTER_EXPR_GET_PRIVATE(self);
    priv->expr = g_strdup("x+y");
    priv->kernel = NULL;

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_input(UFO_FILTER(self), "input1", 2);
    ufo_filter_register_output(UFO_FILTER(self), "output0", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_EXPR, NULL);
}
