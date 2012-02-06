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

typedef enum {
    OP_ADD = 0,
    OP_MUL,
    OP_DIV,
    OP_CONJ,
    OP_N
} ComplexOperation;

static const gchar *operation_map[] = { "add", "mul", "div", "conj" };

struct _UfoFilterComplexPrivate {
     cl_kernel kernels[OP_N];
     ComplexOperation operation;
};

GType ufo_filter_complex_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterComplex, ufo_filter_complex, UFO_TYPE_FILTER);

#define UFO_FILTER_COMPLEX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_COMPLEX, UfoFilterComplexPrivate))

enum {
    PROP_0,
    PROP_OP,
    N_PROPERTIES
};

static GParamSpec *complex_properties[N_PROPERTIES] = { NULL, };

static void ufo_filter_complex_initialize(UfoFilter *filter)
{
    UfoFilterComplex *self = UFO_FILTER_COMPLEX(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;

    ufo_resource_manager_add_program(manager, "complex.cl", NULL, &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
        return;
    }

    self->priv->kernels[OP_ADD] = ufo_resource_manager_get_kernel(manager, "c_add", &error);
    self->priv->kernels[OP_MUL] = ufo_resource_manager_get_kernel(manager, "c_mul", &error);
    self->priv->kernels[OP_DIV] = ufo_resource_manager_get_kernel(manager, "c_div", &error);
    self->priv->kernels[OP_CONJ] = ufo_resource_manager_get_kernel(manager, "c_conj", &error);
}

static void ufo_filter_complex_binary(UfoFilter *filter, cl_kernel kernel)
{
    UfoChannel *input_channel_a = ufo_filter_get_input_channel_by_name(filter, "input0");
    UfoChannel *input_channel_b = ufo_filter_get_input_channel_by_name(filter, "input1");
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    
    UfoBuffer *a = ufo_channel_get_input_buffer(input_channel_a);
    UfoBuffer *b = ufo_channel_get_input_buffer(input_channel_b);
    guint num_dims_a = 0, num_dims_b = 0;
    guint *dim_size_a = NULL;
    guint *dim_size_b = NULL;

    ufo_buffer_get_dimensions(a, &num_dims_a, &dim_size_a);
    ufo_buffer_get_dimensions(b, &num_dims_b, &dim_size_b);
    g_assert(num_dims_a == num_dims_b);
    for (int i = 0; i < num_dims_a; i++)
        g_assert(dim_size_a[i] == dim_size_b[i]);

    ufo_channel_allocate_output_buffers(output_channel, num_dims_a, dim_size_a);
    
    cl_command_queue cmd_queue = ufo_filter_get_command_queue(filter);
    cl_mem mem_a, mem_b, mem_r;
    cl_event wait_event;
    
    size_t global_work_size[2] = { 0, 0 };

    while ((a != NULL) && (b != NULL)) {
        UfoBuffer *r = ufo_channel_get_output_buffer(output_channel);
        
        mem_a = ufo_buffer_get_device_array(a, cmd_queue);
        mem_b = ufo_buffer_get_device_array(b, cmd_queue);
        mem_r = ufo_buffer_get_device_array(r, cmd_queue);
        
        /* Each thread processes the real and the imaginary part */
        global_work_size[0] = dim_size_a[0] / 2;
        global_work_size[1] = dim_size_a[1];
        clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &mem_a);
        clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &mem_b);
        clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *) &mem_r);
        clEnqueueNDRangeKernel(cmd_queue, kernel,
                2, NULL, global_work_size, NULL,
                0, NULL, &wait_event);

        ufo_buffer_attach_event(r, wait_event);
        /* ufo_filter_account_gpu_time(filter, (void **) &event); */

        ufo_channel_finalize_output_buffer(output_channel, r);
        ufo_channel_finalize_input_buffer(input_channel_a, a);
        ufo_channel_finalize_input_buffer(input_channel_b, b);
        a = ufo_channel_get_input_buffer(input_channel_a);
        b = ufo_channel_get_input_buffer(input_channel_b);
    }
    ufo_channel_finish(output_channel);
    g_free(dim_size_a);
    g_free(dim_size_b);
}

static void ufo_filter_complex_unary(UfoFilter* filter, cl_kernel kernel)
{
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    
    cl_command_queue cmd_queue = ufo_filter_get_command_queue(filter);
    cl_event wait_event;
    
    size_t global_work_size[2] = { 0, 0 };
    guint num_dims = 0;
    guint *dim_size = NULL;
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);
    ufo_buffer_get_dimensions(input, &num_dims, &dim_size);
    ufo_channel_allocate_output_buffers(output_channel, num_dims, dim_size);

    while (input != NULL) {
        UfoBuffer *output = ufo_channel_get_output_buffer(output_channel);
        cl_mem input_mem = ufo_buffer_get_device_array(input, cmd_queue);
        cl_mem output_mem = ufo_buffer_get_device_array(output, cmd_queue);
        
        /* Each thread processes the real and the imaginary part */
        global_work_size[0] = dim_size[0] / 2;
        global_work_size[1] = dim_size[1];
        clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &input_mem);
        clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &output_mem);
        clEnqueueNDRangeKernel(cmd_queue, kernel,
                2, NULL, global_work_size, NULL,
                0, NULL, &wait_event);

        ufo_buffer_attach_event(input, wait_event);
        /* ufo_filter_account_gpu_time(filter, (void **) &event); */

        ufo_channel_finalize_output_buffer(output_channel, output);
        ufo_channel_finalize_input_buffer(input_channel, input);
        input = ufo_channel_get_input_buffer(input_channel);
    }
    g_free(dim_size);
    ufo_channel_finish(output_channel);
}

static void ufo_filter_complex_process(UfoFilter *filter)
{
    UfoFilterComplexPrivate *priv = UFO_FILTER_COMPLEX_GET_PRIVATE(filter);
    cl_kernel kernel = priv->kernels[priv->operation];
    
    if (priv->operation == OP_CONJ) 
        ufo_filter_complex_unary(filter, kernel); 
    else
        ufo_filter_complex_binary(filter, kernel);
}

static void ufo_filter_complex_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
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

static void ufo_filter_complex_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
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

static void ufo_filter_complex_class_init(UfoFilterComplexClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_complex_set_property;
    gobject_class->get_property = ufo_filter_complex_get_property;
    filter_class->initialize = ufo_filter_complex_initialize;
    filter_class->process = ufo_filter_complex_process;

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

static void ufo_filter_complex_init(UfoFilterComplex *self)
{
    UfoFilterComplexPrivate *priv = self->priv = UFO_FILTER_COMPLEX_GET_PRIVATE(self);
    priv->operation = OP_ADD;

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
    ufo_filter_register_input(UFO_FILTER(self), "input1", 2);
    ufo_filter_register_output(UFO_FILTER(self), "result", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_COMPLEX, NULL);
}
