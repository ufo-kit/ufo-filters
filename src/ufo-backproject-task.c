/**
 * SECTION:ufo-backproject-task
 * @Short_description: Process arbitrary Backproject kernels
 * @Title: backproject
 *
 * This module is used to load an arbitrary #UfoBackprojectTask:kernel from
 * #UfoBackprojectTask:filename and execute it on each input. The kernel must have
 * only two global float array parameters, the first represents the input, the
 * second one the output. #UfoBackprojectTask:num-dims must be changed, if the kernel
 * accesses either one or three dimensional index spaces.
 */

#ifdef __APPLE__
#include <Backproject/cl.h>
#else
#include <CL/cl.h>
#endif
#include <ufo-gpu-task-iface.h>
#include "ufo-backproject-task.h"

struct _UfoBackprojectTaskPrivate {
    cl_context context;
    cl_kernel kernel;
    cl_mem texture;
    gfloat axis_pos;
    gfloat angle_step;
    guint n_projections;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoBackprojectTask, ufo_backproject_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_BACKPROJECT_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_BACKPROJECT_TASK, UfoBackprojectTaskPrivate))

enum {
    PROP_0,
    PROP_AXIS_POSITION,
    PROP_ANGLE_STEP,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_backproject_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_BACKPROJECT_TASK, NULL));
}

static gboolean
ufo_backproject_task_process (UfoGpuTask *task,
                              UfoBuffer **inputs,
                              UfoBuffer *output,
                              UfoRequisition *requisition,
                              UfoGpuNode *node)
{
    UfoBackprojectTaskPrivate *priv;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_int cl_err;
    gfloat angle_step;
    gfloat axis_pos;
    gsize dest_origin[3] = { 0, 0, 0 };
    gsize dest_region[3] = { 1, 1, 1 };

    priv = UFO_BACKPROJECT_TASK (task)->priv;
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    dest_region[0] = requisition->dims[0];
    dest_region[1] = priv->n_projections;

    /* Guess angle step and axis position if they are not provided by the user. */
    if (priv->angle_step <= 0.0) {
        UfoRequisition in_req;
        ufo_buffer_get_requisition (inputs[0], &in_req);
        angle_step = (gfloat) (G_PI / ((gfloat) in_req.dims[1]));
    }
    else
        angle_step = priv->angle_step;

    if (priv->axis_pos <= 0.0)
        axis_pos = (gfloat) ((gfloat) requisition->dims[0]) / 2.0f;
    else
        axis_pos = priv->axis_pos;

    cl_err = clEnqueueCopyBufferToImage (cmd_queue,
                                         in_mem, priv->texture,
                                         0, dest_origin, dest_region,
                                         0, NULL, NULL);

    UFO_RESOURCES_CHECK_CLERR (cl_err);
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &priv->texture));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (guint),  &priv->n_projections));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (gfloat), &axis_pos));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 4, sizeof (gfloat), &angle_step));

    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue,
                                                       priv->kernel,
                                                       2, NULL, requisition->dims, NULL,
                                                       0, NULL, NULL));

    return TRUE;
}

static void
ufo_backproject_task_setup (UfoTask *task,
                            UfoResources *resources,
                            GError **error)
{
    UfoBackprojectTaskPrivate *priv;

    priv = UFO_BACKPROJECT_TASK_GET_PRIVATE (task);

    priv->context = ufo_resources_get_context (resources);
    priv->kernel = ufo_resources_get_kernel (resources,
                                             "backproject.cl",
                                             "backproject_tex",
                                             error);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
}

static void
ufo_backproject_task_get_requisition (UfoTask *task,
                                      UfoBuffer **inputs,
                                      UfoRequisition *requisition)
{
    UfoBackprojectTaskPrivate *priv;
    UfoRequisition in_req;
    cl_int cl_err;

    priv = UFO_BACKPROJECT_TASK_GET_PRIVATE (task);
    ufo_buffer_get_requisition (inputs[0], &in_req);

    priv->n_projections = (guint) in_req.dims[1];

    if (priv->texture == NULL) {
        cl_image_format format;

        format.image_channel_order = CL_R;
        format.image_channel_data_type = CL_FLOAT;

        priv->texture = clCreateImage2D (priv->context,
                                         CL_MEM_READ_ONLY,
                                         &format,
                                         in_req.dims[0],
                                         in_req.dims[1],
                                         0, NULL, &cl_err);
        UFO_RESOURCES_CHECK_CLERR (cl_err);
    }

    requisition->n_dims = 2;
    requisition->dims[0] = in_req.dims[0];
    requisition->dims[1] = in_req.dims[0];
}

static void
ufo_backproject_task_get_structure (UfoTask *task,
                                    guint *n_inputs,
                                    UfoInputParam **in_params,
                                    UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 1;
    *in_params = g_new0 (UfoInputParam, 1);
    (*in_params)[0].n_dims = 2;
    (*in_params)[0].n_expected = -1;
}

static UfoNode *
ufo_backproject_task_copy_real (UfoNode *node,
                           GError **error)
{
    UfoBackprojectTask *orig;
    UfoBackprojectTask *copy;

    orig = UFO_BACKPROJECT_TASK (node);
    copy = UFO_BACKPROJECT_TASK (ufo_backproject_task_new ());

    g_object_set (G_OBJECT (copy),
                  "axis-pos", orig->priv->axis_pos,
                  "angle-step", orig->priv->angle_step,
                  NULL);

    return UFO_NODE (copy);
}

static gboolean
ufo_backproject_task_equal_real (UfoNode *n1,
                            UfoNode *n2)
{
    g_return_val_if_fail (UFO_IS_BACKPROJECT_TASK (n1) && UFO_IS_BACKPROJECT_TASK (n2), FALSE);
    return UFO_BACKPROJECT_TASK (n1)->priv->kernel == UFO_BACKPROJECT_TASK (n2)->priv->kernel;
}

static void
ufo_backproject_task_finalize (GObject *object)
{
    UfoBackprojectTaskPrivate *priv;

    priv = UFO_BACKPROJECT_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->texture) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->texture));
        priv->texture = NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    G_OBJECT_CLASS (ufo_backproject_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_backproject_task_setup;
    iface->get_requisition = ufo_backproject_task_get_requisition;
    iface->get_structure = ufo_backproject_task_get_structure;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_backproject_task_process;
}

static void
ufo_backproject_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoBackprojectTaskPrivate *priv = UFO_BACKPROJECT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_AXIS_POSITION:
            priv->axis_pos = g_value_get_float (value);
            break;
        case PROP_ANGLE_STEP:
            priv->angle_step = g_value_get_float (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_backproject_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoBackprojectTaskPrivate *priv = UFO_BACKPROJECT_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_AXIS_POSITION:
            g_value_set_float (value, priv->axis_pos);
            break;
        case PROP_ANGLE_STEP:
            g_value_set_float (value, priv->angle_step);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_backproject_task_class_init (UfoBackprojectTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;
    const gfloat limit = (gfloat) (4.0 * G_PI);
    
    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->finalize = ufo_backproject_task_finalize;
    oclass->set_property = ufo_backproject_task_set_property;
    oclass->get_property = ufo_backproject_task_get_property;

    properties[PROP_AXIS_POSITION] =
        g_param_spec_float ("axis-pos",
                            "Position of rotation axis",
                            "Position of rotation axis",
                            -1.0, +8192.0, 0.0f,
                            G_PARAM_READWRITE);

    properties[PROP_ANGLE_STEP] =
        g_param_spec_float ("angle-step",
                            "Increment of angle in radians",
                            "Increment of angle in radians",
                            -limit, +limit, 0.0f,
                            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->copy = ufo_backproject_task_copy_real;
    node_class->equal = ufo_backproject_task_equal_real;

    g_type_class_add_private(klass, sizeof(UfoBackprojectTaskPrivate));
}

static void
ufo_backproject_task_init (UfoBackprojectTask *self)
{
    UfoBackprojectTaskPrivate *priv;
    self->priv = priv = UFO_BACKPROJECT_TASK_GET_PRIVATE (self);
    priv->kernel = NULL;
    priv->axis_pos = -1.0;
    priv->angle_step = -1.0;
    priv->kernel = NULL;
    priv->texture = NULL;
}
