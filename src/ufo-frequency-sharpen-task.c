/*
 * Copyright (C) 2026
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

#include "config.h"

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-frequency-sharpen-task.h"

typedef enum {
    METHOD_LAPLACE = 0,
    METHOD_DISCRETE_LAPLACE,
    METHOD_LORENTZ,
    N_METHODS
} UfoFrequencySharpenMethod;

struct _UfoFrequencySharpenTaskPrivate {
    cl_kernel kernels[N_METHODS];

    gfloat strength;
    gfloat lorentz_fwhm;
    gfloat max_boost;
    UfoFrequencySharpenMethod method;
    gchar *method_name;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoFrequencySharpenTask, ufo_frequency_sharpen_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FREQUENCY_SHARPEN_TASK, UfoFrequencySharpenTaskPrivate))

enum {
    PROP_0,
    PROP_STRENGTH,
    PROP_METHOD,
    PROP_LORENTZ_FWHM,
    PROP_MAX_BOOST,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

static UfoFrequencySharpenMethod
parse_method (const gchar *method)
{
    if (g_strcmp0 (method, "laplace") == 0)
        return METHOD_LAPLACE;

    if (g_strcmp0 (method, "discrete-laplace") == 0)
        return METHOD_DISCRETE_LAPLACE;

    if (g_strcmp0 (method, "lorentz") == 0)
        return METHOD_LORENTZ;

    return N_METHODS;
}

UfoNode *
ufo_frequency_sharpen_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_FREQUENCY_SHARPEN_TASK, NULL));
}

static gboolean
ufo_frequency_sharpen_task_process (UfoTask *task,
                                    UfoBuffer **inputs,
                                    UfoBuffer *output,
                                    UfoRequisition *requisition)
{
    UfoFrequencySharpenTaskPrivate *priv;
    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_mem out_mem;
    cl_kernel kernel;

    priv = UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE (task);
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);
    kernel = priv->kernels[priv->method];

    ufo_buffer_set_layout (output, UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 1, sizeof (cl_mem), &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 2, sizeof (cl_float), &priv->strength));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 3, sizeof (cl_float), &priv->max_boost));

    if (priv->method == METHOD_LORENTZ)
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (kernel, 4, sizeof (cl_float), &priv->lorentz_fwhm));

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));
    ufo_profiler_call (profiler, cmd_queue, kernel, 2, requisition->dims, NULL);

    return TRUE;
}

static void
ufo_frequency_sharpen_task_setup (UfoTask *task,
                                  UfoResources *resources,
                                  GError **error)
{
    UfoFrequencySharpenTaskPrivate *priv;

    priv = UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE (task);

    priv->kernels[METHOD_LAPLACE] = ufo_resources_get_kernel (resources, "sharpening.cl", "frequency_sharpen_laplace", NULL, error);
    priv->kernels[METHOD_DISCRETE_LAPLACE] = ufo_resources_get_kernel (resources, "sharpening.cl", "frequency_sharpen_discrete_laplace", NULL, error);
    priv->kernels[METHOD_LORENTZ] = ufo_resources_get_kernel (resources, "sharpening.cl", "frequency_sharpen_lorentz", NULL, error);

    for (guint i = 0; i < N_METHODS; i++) {
        if (priv->kernels[i] != NULL)
            UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (priv->kernels[i]), error);
    }
}

static void
ufo_frequency_sharpen_task_get_requisition (UfoTask *task,
                                            UfoBuffer **inputs,
                                            UfoRequisition *requisition,
                                            GError **error)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_frequency_sharpen_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_frequency_sharpen_task_get_num_dimensions (UfoTask *task,
                                               guint input)
{
    g_return_val_if_fail (input == 0, 0);
    return 2;
}

static UfoTaskMode
ufo_frequency_sharpen_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

static gboolean
ufo_frequency_sharpen_task_equal_real (UfoNode *n1,
                                       UfoNode *n2)
{
    UfoFrequencySharpenTaskPrivate *priv1;
    UfoFrequencySharpenTaskPrivate *priv2;

    g_return_val_if_fail (UFO_IS_FREQUENCY_SHARPEN_TASK (n1) && UFO_IS_FREQUENCY_SHARPEN_TASK (n2), FALSE);

    priv1 = UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE (n1);
    priv2 = UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE (n2);

    return priv1->kernels[priv1->method] == priv2->kernels[priv2->method];
}

static void
ufo_frequency_sharpen_task_set_property (GObject *object,
                                         guint property_id,
                                         const GValue *value,
                                         GParamSpec *pspec)
{
    UfoFrequencySharpenTaskPrivate *priv;

    priv = UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_STRENGTH:
            priv->strength = g_value_get_float (value);
            break;
        case PROP_METHOD:
            if (parse_method (g_value_get_string (value)) != N_METHODS) {
                g_free (priv->method_name);
                priv->method_name = g_value_dup_string (value);
                priv->method = parse_method (priv->method_name);
            }
            else {
                g_warning ("Unknown frequency sharpening method '%s', keeping '%s'",
                           g_value_get_string (value), priv->method_name);
            }
            break;
        case PROP_LORENTZ_FWHM:
            priv->lorentz_fwhm = g_value_get_float (value);
            break;
        case PROP_MAX_BOOST:
            priv->max_boost = g_value_get_float (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_frequency_sharpen_task_get_property (GObject *object,
                                         guint property_id,
                                         GValue *value,
                                         GParamSpec *pspec)
{
    UfoFrequencySharpenTaskPrivate *priv;

    priv = UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_STRENGTH:
            g_value_set_float (value, priv->strength);
            break;
        case PROP_METHOD:
            g_value_set_string (value, priv->method_name);
            break;
        case PROP_LORENTZ_FWHM:
            g_value_set_float (value, priv->lorentz_fwhm);
            break;
        case PROP_MAX_BOOST:
            g_value_set_float (value, priv->max_boost);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_frequency_sharpen_task_finalize (GObject *object)
{
    UfoFrequencySharpenTaskPrivate *priv;

    priv = UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE (object);

    for (guint i = 0; i < N_METHODS; i++) {
        if (priv->kernels[i]) {
            UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernels[i]));
            priv->kernels[i] = NULL;
        }
    }

    g_free (priv->method_name);
    priv->method_name = NULL;

    G_OBJECT_CLASS (ufo_frequency_sharpen_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_frequency_sharpen_task_setup;
    iface->get_requisition = ufo_frequency_sharpen_task_get_requisition;
    iface->get_num_inputs = ufo_frequency_sharpen_task_get_num_inputs;
    iface->get_num_dimensions = ufo_frequency_sharpen_task_get_num_dimensions;
    iface->get_mode = ufo_frequency_sharpen_task_get_mode;
    iface->process = ufo_frequency_sharpen_task_process;
}

static void
ufo_frequency_sharpen_task_class_init (UfoFrequencySharpenTaskClass *klass)
{
    GObjectClass *oclass;
    UfoNodeClass *node_class;

    oclass = G_OBJECT_CLASS (klass);
    node_class = UFO_NODE_CLASS (klass);

    oclass->set_property = ufo_frequency_sharpen_task_set_property;
    oclass->get_property = ufo_frequency_sharpen_task_get_property;
    oclass->finalize = ufo_frequency_sharpen_task_finalize;

    properties[PROP_STRENGTH] =
        g_param_spec_float ("strength",
            "Sharpening strength",
            "Strength of the frequency-domain sharpening filter",
            0.0f, G_MAXFLOAT, 1.0f,
            G_PARAM_READWRITE);

    properties[PROP_METHOD] =
        g_param_spec_string ("method",
            "Sharpening method",
            "Frequency-domain sharpening method",
            "laplace",
            G_PARAM_READWRITE);

    properties[PROP_LORENTZ_FWHM] =
        g_param_spec_float ("lorentz-fwhm",
            "Lorentz FWHM",
            "Full width at half maximum of the Lorentz blur kernel",
            0.0f, G_MAXFLOAT, 1.0f,
            G_PARAM_READWRITE);

    properties[PROP_MAX_BOOST] =
        g_param_spec_float ("max-boost",
            "Maximum boost",
            "Maximum additional sharpening boost; 0 disables tanh limiting",
            0.0f, G_MAXFLOAT, 0.0f,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    node_class->equal = ufo_frequency_sharpen_task_equal_real;

    g_type_class_add_private (klass, sizeof (UfoFrequencySharpenTaskPrivate));
}

static void
ufo_frequency_sharpen_task_init (UfoFrequencySharpenTask *self)
{
    UfoFrequencySharpenTaskPrivate *priv;

    self->priv = priv = UFO_FREQUENCY_SHARPEN_TASK_GET_PRIVATE (self);

    for (guint i = 0; i < N_METHODS; i++)
        priv->kernels[i] = NULL;

    priv->strength = 1.0f;
    priv->lorentz_fwhm = 1.0f;
    priv->max_boost = 0.0f;
    priv->method = METHOD_LAPLACE;
    priv->method_name = g_strdup ("laplace");
}
