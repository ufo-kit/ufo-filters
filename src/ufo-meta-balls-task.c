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

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ufo-meta-balls-task.h"

/**
 * SECTION:ufo-meta-balls-task
 * @Short_description: Generate animated meta balls
 * @Title: meta-balls
 *
 * Generate a stream of meta balls on a two-dimensional grid.
 */

struct _UfoMetaBallsTaskPrivate {
    cl_context  context;
    cl_kernel   kernel;
    cl_mem      positions_mem;
    cl_mem      sizes_mem;

    guint width;
    guint height;
    guint num_balls;
    guint num_iterations;
    guint current_iteration;
    gboolean run_infinitely;
    guint frames_per_second;
    gsize num_position_bytes;

    GTimer *timer;
    gdouble seconds_per_frame;

    gfloat *positions;
    gfloat *velocities;
    gfloat *sizes;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_gpu_task_interface_init (UfoGpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoMetaBallsTask, ufo_meta_balls_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_GPU_TASK,
                                                ufo_gpu_task_interface_init))

#define UFO_META_BALLS_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_META_BALLS_TASK, UfoMetaBallsTaskPrivate))

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_NUM_BALLS,
    PROP_NUM_ITERATIONS,
    PROP_RUN_INFINITELY,
    PROP_FRAMES_PER_SECOND,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_meta_balls_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_META_BALLS_TASK, NULL));
}

static void
ufo_meta_balls_task_setup (UfoTask *task,
                           UfoResources *resources,
                           GError **error)
{
    UfoMetaBallsTaskPrivate *priv;
    gfloat f_width;
    gfloat f_height;
    gsize size;
    cl_int err = CL_SUCCESS;

    priv = UFO_META_BALLS_TASK_GET_PRIVATE (task);
    priv->context = ufo_resources_get_context (resources);

    priv->kernel = ufo_resources_get_kernel (resources,
                                             "metaballs.cl",
                                             "draw_metaballs",
                                             error);

    UFO_RESOURCES_CHECK_CLERR (clRetainContext (priv->context));

    if (priv->kernel != NULL)
        UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));

    size = priv->num_balls * sizeof (gfloat);

    priv->current_iteration = 0;
    priv->seconds_per_frame = 1.0 / ((gdouble) priv->frames_per_second);
    priv->num_position_bytes = 2 * size;
    priv->positions = g_malloc0 (priv->num_position_bytes);
    priv->velocities = g_malloc0 (priv->num_position_bytes);
    priv->sizes = g_malloc0 (size);
    priv->timer = g_timer_new ();

    f_width = (gfloat) priv->width;
    f_height = (gfloat) priv->height;

    for (guint i = 0; i < priv->num_balls; i++) {
        const guint x = 2*i, y = 2*i + 1;
        priv->sizes[i] = (gfloat) g_random_double_range (f_width / 50.0f, f_width / 10.0f);
        priv->positions[x] = (gfloat) g_random_double_range (0.0, (double) f_width);
        priv->positions[y] = (gfloat) g_random_double_range (0.0, (double) f_height);
        priv->velocities[x] = (gfloat) g_random_double_range (-4.0, 4.0);
        priv->velocities[y] = (gfloat) g_random_double_range (-4.0, 4.0);
    };

    priv->positions_mem = clCreateBuffer(priv->context,
                                         CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                         priv->num_position_bytes, priv->positions, &err);
    UFO_RESOURCES_CHECK_CLERR (err);

    priv->sizes_mem = clCreateBuffer(priv->context,
                                     CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     size, priv->sizes, &err);
    UFO_RESOURCES_CHECK_CLERR (err);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &priv->positions_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_mem), &priv->sizes_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, sizeof (cl_uint), &priv->num_balls));
}

static void
ufo_meta_balls_task_get_requisition (UfoTask *task,
                                     UfoBuffer **inputs,
                                     UfoRequisition *requisition)
{
    UfoMetaBallsTaskPrivate *priv;

    priv = UFO_META_BALLS_TASK_GET_PRIVATE (task);
    requisition->n_dims = 2;
    requisition->dims[0] = priv->width;
    requisition->dims[1] = priv->height;
}

static void
ufo_meta_balls_task_get_structure (UfoTask *task,
                                   guint *n_inputs,
                                   UfoInputParam **in_params,
                                   UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_SINGLE;
    *n_inputs = 0;
}

static gboolean
ufo_meta_balls_task_process (UfoGpuTask *task,
                             UfoBuffer **inputs,
                             UfoBuffer *output,
                             UfoRequisition *requisition,
                             UfoGpuNode *node)
{
    UfoMetaBallsTaskPrivate *priv;
    cl_command_queue cmd_queue;
    cl_mem out_mem;
    
    priv = UFO_META_BALLS_TASK_GET_PRIVATE (task);

    if (!priv->run_infinitely && (priv->current_iteration++) >= priv->num_iterations)
        return FALSE;

    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    out_mem = ufo_buffer_get_device_array (output, cmd_queue);

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof(cl_mem), (cl_mem) &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clEnqueueNDRangeKernel (cmd_queue,
                                                       priv->kernel,
                                                       2, NULL, requisition->dims, NULL,
                                                       0, NULL, NULL));

    /* Update positions and velocities */
    for (guint j = 0; j < priv->num_balls; j++) {
        const guint x = 2*j, y = 2*j + 1;

        priv->positions[x] += priv->velocities[x];
        priv->positions[y] += priv->velocities[y];

        if (priv->positions[x] < 0 || priv->positions[x] > priv->width)
            priv->velocities[x] = -priv->velocities[x];

        if (priv->positions[y] < 0 || priv->positions[y] > priv->height)
            priv->velocities[y] = -priv->velocities[y];
    }

    UFO_RESOURCES_CHECK_CLERR (clEnqueueWriteBuffer (cmd_queue,
                                                     priv->positions_mem,
                                                     CL_FALSE,
                                                     0, priv->num_position_bytes, priv->positions,
                                                     0, NULL, NULL));

    g_timer_stop(priv->timer);

    if (priv->frames_per_second > 0) {
        const gdouble elapsed = g_timer_elapsed (priv->timer, NULL);
        const gdouble delta = priv->seconds_per_frame - elapsed;

        if (delta > 0.0)
            g_usleep (G_USEC_PER_SEC * ((gulong) delta));
    }

    return TRUE;
}

static void
ufo_meta_balls_task_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
    UfoMetaBallsTaskPrivate *priv = UFO_META_BALLS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_WIDTH:
            priv->width = g_value_get_uint(value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint(value);
            break;
        case PROP_NUM_BALLS:
            priv->num_balls = g_value_get_uint(value);
            break;
        case PROP_NUM_ITERATIONS:
            priv->num_iterations = g_value_get_uint(value);
            break;
        case PROP_RUN_INFINITELY:
            priv->run_infinitely = g_value_get_boolean(value);
            break;
        case PROP_FRAMES_PER_SECOND:
            priv->frames_per_second = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_meta_balls_task_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
    UfoMetaBallsTaskPrivate *priv = UFO_META_BALLS_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_WIDTH:
            g_value_set_uint(value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint(value, priv->height);
            break;
        case PROP_NUM_BALLS:
            g_value_set_uint(value, priv->num_balls);
            break;
        case PROP_NUM_ITERATIONS:
            g_value_set_uint(value, priv->num_iterations);
            break;
        case PROP_RUN_INFINITELY:
            g_value_set_boolean(value, priv->run_infinitely);
            break;
        case PROP_FRAMES_PER_SECOND:
            g_value_set_uint(value, priv->frames_per_second);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_meta_balls_task_finalize (GObject *object)
{
    UfoMetaBallsTaskPrivate *priv;

    priv = UFO_META_BALLS_TASK_GET_PRIVATE (object);

    if (priv->kernel) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));
        priv->kernel = NULL;
    }

    if (priv->positions_mem) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->positions_mem));
        priv->positions_mem= NULL;
    }

    if (priv->sizes_mem) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->sizes_mem));
        priv->sizes_mem= NULL;
    }

    if (priv->context) {
        UFO_RESOURCES_CHECK_CLERR (clReleaseContext (priv->context));
        priv->context = NULL;
    }

    g_free (priv->sizes);
    g_free (priv->positions);
    g_free (priv->velocities);
    g_timer_destroy (priv->timer);

    G_OBJECT_CLASS (ufo_meta_balls_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_meta_balls_task_setup;
    iface->get_structure = ufo_meta_balls_task_get_structure;
    iface->get_requisition = ufo_meta_balls_task_get_requisition;
}

static void
ufo_gpu_task_interface_init (UfoGpuTaskIface *iface)
{
    iface->process = ufo_meta_balls_task_process;
}

static void
ufo_meta_balls_task_class_init (UfoMetaBallsTaskClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->set_property = ufo_meta_balls_task_set_property;
    gobject_class->get_property = ufo_meta_balls_task_get_property;
    gobject_class->finalize = ufo_meta_balls_task_finalize;

    properties[PROP_WIDTH] =
        g_param_spec_uint("width",
                "Width of the output",
                "Width of the output",
                1, 8192, 512,
                G_PARAM_READWRITE);

    properties[PROP_HEIGHT] =
        g_param_spec_uint("height",
                "Height of the output",
                "Height of the output",
                1, 8192, 512,
                G_PARAM_READWRITE);

    properties[PROP_NUM_BALLS] =
        g_param_spec_uint("num-balls",
                "Number of meta balls",
                "Number of meta balls",
                1, 256, 1,
                G_PARAM_READWRITE);

    properties[PROP_NUM_ITERATIONS] =
        g_param_spec_uint("num-iterations",
                "Number of iterations",
                "Number of iterations",
                1, G_MAXUINT, 1,
                G_PARAM_READWRITE);

    properties[PROP_RUN_INFINITELY] =
        g_param_spec_boolean("run-infinitely",
                "Run infinitely",
                "Run infinitely",
                FALSE,
                G_PARAM_READWRITE);

    properties[PROP_FRAMES_PER_SECOND] =
        g_param_spec_uint("frames-per-second",
                "Number of frames per second (0 for maximum possible rate)",
                "Number of frames per second (0 for maximum possible rate)",
                0, G_MAXINT, 0,
                G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (gobject_class, i, properties[i]);

    g_type_class_add_private (gobject_class, sizeof(UfoMetaBallsTaskPrivate));
}

static void
ufo_meta_balls_task_init(UfoMetaBallsTask *self)
{
    UfoMetaBallsTaskPrivate *priv;

    self->priv = priv = UFO_META_BALLS_TASK_GET_PRIVATE(self);
    priv->width = 512;
    priv->height = 512;
    priv->num_balls = 1;
    priv->num_iterations = 1;
    priv->run_infinitely = FALSE;
    priv->frames_per_second = 0;
}
