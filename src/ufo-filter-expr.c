/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

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
 * @Short_description: Generic mathematical expression filter
 * @Title: expr
 *
 * Detailed description.
 */

struct _UfoFilterExprPrivate {
    gchar *expr;
    cl_kernel kernel;
    gsize global_work_size[2];
};

G_DEFINE_TYPE(UfoFilterExpr, ufo_filter_expr, UFO_TYPE_FILTER)

#define UFO_FILTER_EXPR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_EXPR, UfoFilterExprPrivate))

enum {
    PROP_0,
    PROP_EXPRESSION,
    N_PROPERTIES
};

static GParamSpec *expr_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_expr_initialize(UfoFilter *filter, UfoBuffer *inputs[], guint **dims, GError **error)
{
    UfoFilterExprPrivate *priv = UFO_FILTER_EXPR_GET_PRIVATE(filter);
    UfoResourceManager *manager;
    guint width_x, height_x, width_y, height_y;

    ufo_buffer_get_2d_dimensions(inputs[0], &width_x, &height_x);
    ufo_buffer_get_2d_dimensions(inputs[1], &width_y, &height_y);

    if ((width_x != width_y) || (height_x != height_y)) {
        /* TODO: issue real error here */
        g_error("inputs must match");
    }

    priv->global_work_size[0] = width_x;
    priv->global_work_size[1] = height_x;
    dims[0][0] = width_x;
    dims[0][1] = height_x;

    gchar *ocl_kernel_source = parse_expression(priv->expr);
    manager = ufo_filter_get_resource_manager (filter);
    priv->kernel = ufo_resource_manager_get_kernel_from_source(manager,
            ocl_kernel_source, "binary_foo_kernel_2b03c582", error);
    g_free(ocl_kernel_source);
}

static void
ufo_filter_expr_process_gpu(UfoFilter *filter, UfoBuffer *inputs[], UfoBuffer *outputs[], GError **error)
{
    UfoFilterExprPrivate *priv = UFO_FILTER_EXPR_GET_PRIVATE(filter);
    cl_command_queue cmd_queue = ufo_filter_get_command_queue (filter);
    cl_mem x_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    cl_mem y_mem = ufo_buffer_get_device_array (inputs[1], cmd_queue);
    cl_mem output_mem = ufo_buffer_get_device_array (outputs[0], cmd_queue);

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), &x_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), &y_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 2, sizeof(cl_mem), &output_mem));

    ufo_profiler_call (ufo_filter_get_profiler (filter),
                       cmd_queue, priv->kernel,
                       2, priv->global_work_size, NULL);
}

static void
ufo_filter_expr_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
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

static void
ufo_filter_expr_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
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

static void
ufo_filter_expr_finalize(GObject *object)
{
    UfoFilterExprPrivate *priv = UFO_FILTER_EXPR_GET_PRIVATE(object);
    g_free(priv->expr);
}

static void
ufo_filter_expr_class_init(UfoFilterExprClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_expr_set_property;
    gobject_class->get_property = ufo_filter_expr_get_property;
    gobject_class->finalize = ufo_filter_expr_finalize;
    filter_class->initialize = ufo_filter_expr_initialize;
    filter_class->process_gpu = ufo_filter_expr_process_gpu;
    
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

static void
ufo_filter_expr_init(UfoFilterExpr *self)
{
    UfoFilterExprPrivate *priv = self->priv = UFO_FILTER_EXPR_GET_PRIVATE(self);
    UfoInputParameter input_params[] = {
        {2, UFO_FILTER_INFINITE_INPUT},
        {2, UFO_FILTER_INFINITE_INPUT}};
    UfoOutputParameter output_params[] = {{2}};

    priv->expr = g_strdup("x+y");
    priv->kernel = NULL;

    ufo_filter_register_inputs (UFO_FILTER (self), 2, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_EXPR, NULL);
}
