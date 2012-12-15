/**
 * SECTION:ufo-filter-task
 * @Short_description: Process arbitrary Filter kernels
 * @Title: filter
 *
 * This module is used to load an arbitrary #UfoFilterTask:kernel from
 * #UfoFilterTask:filename and execute it on each input. The kernel must have
 * only two global float array parameters, the first represents the input, the
 * second one the output. #UfoFilterTask:num-dims must be changed, if the kernel
 * accesses either one or three dimensional index spaces.
 */

#ifdef __APPLE__
#include <Filter/cl.h>
#else
#include <CL/cl.h>
#endif
#include <ufo-gpu-task-iface.h>
#include "ufo-filter-task.h"

struct _UfoFilterTaskPrivate {
    cl_context context;
    cl_kernel kernel;
    cl_mem  filter_mem;
    gfloat  bw_cutoff;
    gfloat  bw_order;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFilterTask, ufo_filter_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_FILTER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_TASK, UfoFilterTaskPrivate))

enum {
    PROP_0,
    N_PROPERTIES
};

/* static GParamSpec *properties[N_PROPERTIES] = { NULL, }; */

UfoNode *
ufo_filter_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FILTER_TASK, NULL));
}

static gboolean
ufo_filter_task_process (UfoGpuTask *task,
                         UfoBuffer **inputs,
                         UfoBuffer *output,
                         UfoRequisition *requisition,
                         UfoGpuNode *node)
{
    UfoFilterTaskPrivate *priv;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;

    priv = UFO_FILTER_TASK (task)->priv;
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_mem), &priv->filter_mem));

    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue,
                                                       priv->kernel,
                                                       2, NULL, requisition->dims, NULL,
                                                       0, NULL, NULL));

    return TRUE;
}

static void
ufo_filter_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoFilterTaskPrivate *priv;

    priv = UFO_FILTER_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    priv->kernel = ufo_resources_get_kernel (resources,
                                             "filter.cl",
                                             "filter",
                                             error);

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));

}

static void 
mirror_coefficients (gfloat *filter, guint width)
{
    for (guint k = width/2; k < width; k += 2) {
        filter[k] = filter[width - k];
        filter[k + 1] = filter[width - k + 1];
    }
}

static gfloat *
compute_ramp_coefficients (guint width)
{
    gfloat *filter = g_malloc0 (width * sizeof (gfloat));
    gfloat scale = 0.5f / ((gfloat) width) / 2.0f;

    for (guint k = 1; k < width / 4; k++) {
        filter[2*k] = ((gfloat) k) * scale;
        filter[2*k + 1] = filter[2*k];
    }

    mirror_coefficients (filter, width);
    return filter;
}

static void
ufo_filter_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    UfoFilterTaskPrivate *priv;

    priv = UFO_FILTER_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], requisition);

    if (priv->filter_mem == NULL) {
        cl_int cl_err;
        gfloat *coefficients;

        coefficients = compute_ramp_coefficients ((guint) requisition->dims[0]);
        priv->filter_mem = clCreateBuffer (priv->context,
                                           CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
                                           requisition->dims[0] * sizeof(float),
                                           coefficients,
                                           &cl_err);
        UFO_RESOURCES_CHECK_CLERR (cl_err);
        g_free (coefficients);
    }
}

static void
ufo_filter_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               guint **n_dims,
                               UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 1;
    *n_dims = g_new0 (guint, 1);
    (*n_dims)[0] = 2;
}

static UfoNode *
ufo_filter_task_copy_real (UfoNode *node,
                           GError **error)
{
    UfoFilterTask *orig;
    UfoFilterTask *copy;

    orig = UFO_FILTER_TASK (node);
    copy = UFO_FILTER_TASK (ufo_filter_task_new ());

    copy->priv->bw_order = orig->priv->bw_order;
    copy->priv->bw_cutoff = orig->priv->bw_cutoff;

    return UFO_NODE (copy);
}

static gboolean
ufo_filter_task_equal_real (UfoNode *n1,
                            UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_FILTER_TASK (n1) && UFO_IS_FILTER_TASK (n2), FALSE);
    return TRUE;
}

static void
ufo_filter_task_finalize (GObject *object)
{
    UfoFilterTaskPrivate *priv;

    priv = UFO_FILTER_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        clReleaseKernel (priv->kernel);
        priv->kernel = NULL;
    }

    if (priv->filter_mem) {
        clReleaseMemObject (priv->filter_mem);
        priv->filter_mem = NULL;
    }

    G_OBJECT_CLASS (ufo_filter_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_filter_task_setup;
    iface->get_requisition = ufo_filter_task_get_requisition;
    iface->get_structure = ufo_filter_task_get_structure;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_filter_task_process;
}

static void
ufo_filter_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    /* UfoFilterTaskPrivate *priv = UFO_FILTER_TASK_GET_PRIVATE (object); */

    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    /* UfoFilterTaskPrivate *priv = UFO_FILTER_TASK_GET_PRIVATE (object); */

    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_task_class_init (UfoFilterTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;
    
    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->finalize = ufo_filter_task_finalize;
    oclass->set_property = ufo_filter_task_set_property;
    oclass->get_property = ufo_filter_task_get_property;

    /* for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++) */
    /*     g_object_class_install_property (oclass, i, properties[i]); */

    node_class->copy = ufo_filter_task_copy_real;
    node_class->equal = ufo_filter_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoFilterTaskPrivate));
}

static void
ufo_filter_task_init (UfoFilterTask *self)
{
    UfoFilterTaskPrivate *priv;
    self->priv = priv = UFO_FILTER_TASK_GET_PRIVATE (self);
    priv->kernel = NULL;
    priv->filter_mem = NULL;
}
