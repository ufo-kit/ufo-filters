/**
 * SECTION:ufo-opencl-task
 * @Short_description: Process arbitrary OpenCL kernels
 * @Title: opencl
 *
 * This module is used to load an arbitrary #UfoOpenCLTask:kernel from
 * #UfoOpenCLTask:filename and execute it on each input. The kernel must have
 * only two global float array parameters, the first represents the input, the
 * second one the output. #UfoOpenCLTask:num-dims must be changed, if the kernel
 * accesses either one or three dimensional index spaces.
 */

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif
#include <ufo-gpu-task-iface.h>
#include "ufo-opencl-task.h"

struct _UfoOpenCLTaskPrivate {
    cl_kernel kernel;
    gchar *filename;
    gchar *funcname;
    guint n_dims;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoOpenCLTask, ufo_opencl_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_OPENCL_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_OPENCL_TASK, UfoOpenCLTaskPrivate))

enum {
    PROP_0,
    PROP_FILENAME,
    PROP_KERNEL,
    PROP_NUM_DIMS,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_opencl_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_OPENCL_TASK, NULL));
}

static gboolean
ufo_opencl_task_process (UfoGpuTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition,
                         UfoGpuNode *node)
{
    UfoOpenCLTaskPrivate *priv;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_event event;

    priv = UFO_OPENCL_TASK (task)->priv;
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &out_mem));

    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue,
                                                       priv->kernel,
                                                       priv->n_dims, NULL, requisition->dims, NULL,
                                                       0, NULL, &event));

    UFO_RESOURCES_CHECK_CLERR (clWaitForEvents (1, &event));
    UFO_RESOURCES_CHECK_CLERR (clReleaseEvent (event));

    return TRUE;
}

static void
ufo_opencl_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoOpenCLTaskPrivate *priv;

    priv = UFO_OPENCL_TASK_GET_PRIVATE (task);

    if (priv->filename == NULL) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "Property ::filename not specified");
        return;
    }

    if (priv->funcname == NULL) {
        g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP,
                     "Property ::kernel not specified");
        return;
    }

    priv->kernel = ufo_resources_get_kernel (resources,
                                             priv->filename,
                                             priv->funcname,
                                             error);

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
}

static void
ufo_opencl_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static void
ufo_opencl_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               UfoInputParam **in_params,
                               UfoTaskMode *mode)
{
    UfoOpenCLTaskPrivate *priv;

    priv = UFO_OPENCL_TASK_GET_PRIVATE (task);
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = priv->n_dims;
}

static UfoNode *
ufo_opencl_task_copy_real (UfoNode *node,
                           GError **error)
{
    UfoOpenCLTask *orig;
    UfoOpenCLTask *copy;

    orig = UFO_OPENCL_TASK (node);
    copy = UFO_OPENCL_TASK (ufo_opencl_task_new ());

    g_object_set (G_OBJECT (copy),
                  "filename", orig->priv->filename,
                  "kernel", orig->priv->funcname,
                  "num-dims", orig->priv->n_dims,
                  NULL);

    return UFO_NODE (copy);
}

static gboolean
ufo_opencl_task_equal_real (UfoNode *n1,
                            UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_OPENCL_TASK (n1) && UFO_IS_OPENCL_TASK (n2), FALSE);
    return UFO_OPENCL_TASK (n1)->priv->kernel == UFO_OPENCL_TASK (n2)->priv->kernel;
}

static void
ufo_opencl_task_finalize (GObject *object)
{
    UfoOpenCLTaskPrivate *priv;

    priv = UFO_OPENCL_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        clReleaseKernel (priv->kernel);
        priv->kernel = NULL;
    }

    g_free (priv->filename);
    g_free (priv->funcname);

    priv->filename = NULL;
    priv->funcname = NULL;
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_opencl_task_setup;
    iface->get_requisition = ufo_opencl_task_get_requisition;
    iface->get_structure = ufo_opencl_task_get_structure;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_opencl_task_process;
}

static void
ufo_opencl_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoOpenCLTaskPrivate *priv = UFO_OPENCL_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FILENAME:
            g_free (priv->filename);
            priv->filename = g_value_dup_string (value);
            break;
        case PROP_KERNEL:
            g_free (priv->funcname);
            priv->funcname = g_value_dup_string (value);
            break;
        case PROP_NUM_DIMS:
            priv->n_dims = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_opencl_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoOpenCLTaskPrivate *priv = UFO_OPENCL_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FILENAME:
            g_value_set_string (value, priv->filename);
            break;
        case PROP_KERNEL:
            g_value_set_string (value, priv->funcname);
            break;
        case PROP_NUM_DIMS:
            g_value_set_uint (value, priv->n_dims);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_opencl_task_class_init (UfoOpenCLTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;
    
    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->finalize = ufo_opencl_task_finalize;
    oclass->set_property = ufo_opencl_task_set_property;
    oclass->get_property = ufo_opencl_task_get_property;

    properties[PROP_FILENAME] =
        g_param_spec_string ("filename",
            "OpenCL kernel filename",
            "OpenCL kernel filename",
            "",
            G_PARAM_READWRITE);

    properties[PROP_KERNEL] =
        g_param_spec_string ("kernel",
            "Kernel name",
            "Name of the kernel that should be computed with this task",
            "",
            G_PARAM_READWRITE);

    properties[PROP_NUM_DIMS] = 
        g_param_spec_uint ("num-dims",
                           "Number of dimensions",
                           "Number of dimensions that the kernel works on",
                           1, 3, 2,
                           G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->copy = ufo_opencl_task_copy_real;
    node_class->equal = ufo_opencl_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoOpenCLTaskPrivate));
}

static void
ufo_opencl_task_init (UfoOpenCLTask *self)
{
    UfoOpenCLTaskPrivate *priv;
    self->priv = priv = UFO_OPENCL_TASK_GET_PRIVATE (self);
    priv->kernel = NULL;
    priv->filename = NULL;
    priv->funcname = NULL;
    priv->n_dims = 2;
}
