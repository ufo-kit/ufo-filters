#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-complex.h"

/**
 * SECTION:ufo-filter-complex
 * @Short_description: Complex arithmetics
 * @Title: complex
 *
 * The reader node loads single files from disk and provides them as a stream in
 * output "image".
 */

typedef enum {
    OP_ADD = 0,
    OP_MUL,
    OP_DIV,
    OP_CONJ,
    OP_N
} ComplexOperation;

static const gchar *operation_map[] = { "add", "mul", "div", "conj" };

struct _UfoFilterComplexPrivate {
     ComplexOperation   operation;
     cl_kernel  kernels[OP_N];
     size_t     global_work_size[2];
};

G_DEFINE_TYPE(UfoFilterComplex, ufo_filter_complex, UFO_TYPE_FILTER)

#define UFO_FILTER_COMPLEX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_COMPLEX, UfoFilterComplexPrivate))

enum {
    PROP_0,
    PROP_OP,
    N_PROPERTIES
};

static GParamSpec *complex_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_complex_initialize(UfoFilter *filter, UfoBuffer *input[], guint **dims, GError **error)
{
    UfoFilterComplexPrivate *priv = UFO_FILTER_COMPLEX_GET_PRIVATE (filter);
    UfoResourceManager *manager = ufo_filter_get_resource_manager(filter);
    GError *tmp_error = NULL;
    guint width, height;

    /* TODO: handle each error independently, maybe write a macro that returns
     * or use g_return_val_if_fail() */
    priv->kernels[OP_ADD] = ufo_resource_manager_get_kernel(manager, "complex.cl", "c_add", &tmp_error);
    priv->kernels[OP_MUL] = ufo_resource_manager_get_kernel(manager, "complex.cl", "c_mul", &tmp_error);
    priv->kernels[OP_DIV] = ufo_resource_manager_get_kernel(manager, "complex.cl", "c_div", &tmp_error);
    priv->kernels[OP_CONJ] = ufo_resource_manager_get_kernel(manager, "complex.cl", "c_conj", &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error (error, tmp_error);
        return;
    }

    /* TODO: Check that second buffer has the same size */
    ufo_buffer_get_2d_dimensions (input[0], &width, &height);
    priv->global_work_size[0] = width / 2;
    priv->global_work_size[1] = height;
    dims[0][0] = width;
    dims[0][1] = height;
}

static void
ufo_filter_complex_binary(UfoFilter *filter, UfoBuffer *inputs[], UfoBuffer *outputs[], cl_command_queue cmd_queue)
{
    UfoFilterComplexPrivate *priv = UFO_FILTER_COMPLEX_GET_PRIVATE (filter);
    cl_kernel kernel = priv->kernels[priv->operation];
    
    cl_mem mem_a = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    cl_mem mem_b = ufo_buffer_get_device_array(inputs[1], cmd_queue);
    cl_mem mem_r = ufo_buffer_get_device_array(outputs[0], cmd_queue);
        
        /* Each thread processes the real and the imaginary part */
        /* global_work_size[0] = dim_size_a[0] / 2; */
        /* global_work_size[1] = dim_size_a[1]; */
    clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &mem_a);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &mem_b);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *) &mem_r);
    clEnqueueNDRangeKernel(cmd_queue, kernel,
            2, NULL, priv->global_work_size, NULL,
            0, NULL, NULL);
}

static void
ufo_filter_complex_unary(UfoFilter* filter, UfoBuffer *inputs[], UfoBuffer *outputs[], cl_command_queue cmd_queue)
{
    UfoFilterComplexPrivate *priv = UFO_FILTER_COMPLEX_GET_PRIVATE (filter);
    cl_kernel kernel = priv->kernels[priv->operation];
    cl_mem input_mem = ufo_buffer_get_device_array(inputs[0], cmd_queue);
    cl_mem output_mem = ufo_buffer_get_device_array(outputs[0], cmd_queue);
    
    clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &input_mem);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &output_mem);
    clEnqueueNDRangeKernel(cmd_queue, kernel,
            2, NULL, priv->global_work_size, NULL,
            0, NULL, NULL);
}

static UfoEventList *
ufo_filter_complex_process_gpu(UfoFilter *filter, UfoBuffer *inputs[], UfoBuffer *outputs[], gpointer cmd_queue, GError **error)
{
    UfoFilterComplexPrivate *priv = UFO_FILTER_COMPLEX_GET_PRIVATE (filter);
    cl_command_queue queue = (cl_command_queue) cmd_queue;

    if (priv->operation == OP_CONJ) 
        ufo_filter_complex_unary(filter, inputs, outputs, queue);
    else
        ufo_filter_complex_binary(filter, inputs, outputs, queue);

    return NULL;
}

static void
ufo_filter_complex_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterComplex *self = UFO_FILTER_COMPLEX(object);
    gchar *op_string = NULL;

    switch (property_id) {
        case PROP_OP:
            op_string = g_strdup(g_value_get_string(value));
            for (int i = 0; i < OP_N; i++) {
                if (!g_strcmp0(op_string, operation_map[i])) {
                    self->priv->operation = i;
                    g_free(op_string);
                    return;
                }
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_complex_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterComplex *self = UFO_FILTER_COMPLEX(object);

    switch (property_id) {
        case PROP_OP:
            g_value_set_string(value, operation_map[self->priv->operation]);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_complex_class_init(UfoFilterComplexClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_complex_set_property;
    gobject_class->get_property = ufo_filter_complex_get_property;
    filter_class->initialize = ufo_filter_complex_initialize;
    filter_class->process_gpu = ufo_filter_complex_process_gpu;

    /* install properties */
    complex_properties[PROP_OP] = 
        g_param_spec_string("operation",
            "Complex operation from [\"add\", \"mul\", \"div\", \"conj\"]",
            "Complex operation from [\"add\", \"mul\", \"div\", \"conj\"]",
            "add",
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_OP, complex_properties[PROP_OP]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterComplexPrivate));
}

static void
ufo_filter_complex_init(UfoFilterComplex *self)
{
    UfoFilterComplexPrivate *priv = self->priv = UFO_FILTER_COMPLEX_GET_PRIVATE(self);
    UfoInputParameter input_params[] = {
        {2, UFO_FILTER_INFINITE_INPUT},
        {2, UFO_FILTER_INFINITE_INPUT}};
    UfoOutputParameter output_params[] = {{2}};

    priv->operation = OP_ADD;

    ufo_filter_register_inputs (UFO_FILTER (self), 2, input_params);
    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT
UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_COMPLEX, NULL);
}
