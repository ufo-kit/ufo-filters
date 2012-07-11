#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-meta-balls.h"

/**
 * SECTION:ufo-filter-meta-balls
 * @Short_description: Generate sample data
 * @Title: metaballs
 *
 * Generate randomized sample data. This node provides so-called meta balls, a
 * physically incorrect approximation of merging perfect-circled bubbles.
 */

struct _UfoFilterMetaBallsPrivate {
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
    gsize global_work_size[2];

    GTimer *timer;
    gdouble seconds_per_frame;

    gfloat *positions;
    gfloat *velocities;
    gfloat *sizes;
};

G_DEFINE_TYPE(UfoFilterMetaBalls, ufo_filter_meta_balls, UFO_TYPE_FILTER_SOURCE)

#define UFO_FILTER_META_BALLS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_META_BALLS, UfoFilterMetaBallsPrivate))

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

static GParamSpec *meta_balls_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_meta_balls_initialize(UfoFilterSource *filter, guint **dims, GError **error)
{
    UfoFilterMetaBallsPrivate *priv = UFO_FILTER_META_BALLS_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *tmp_error = NULL;
    priv->kernel = ufo_resource_manager_get_kernel(manager, "metaballs.cl", "draw_metaballs", &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error (error, tmp_error);
        return;
    }

    cl_context context = ufo_resource_manager_get_context(manager);
    const gsize num_sizes_bytes = priv->num_balls * sizeof(float);

    priv->current_iteration = 0;
    priv->seconds_per_frame = 1.0 / ((gdouble) priv->frames_per_second);
    priv->num_position_bytes = 2 * priv->num_balls * sizeof(float);
    priv->positions = g_malloc0(priv->num_position_bytes);
    priv->velocities = g_malloc0(priv->num_position_bytes);
    priv->sizes = g_malloc0(num_sizes_bytes);
    priv->timer = g_timer_new();
    priv->global_work_size[0] = priv->width;
    priv->global_work_size[1] = priv->height;
    dims[0][0] = priv->width;
    dims[0][1] = priv->height;

    const gfloat f_width = (gfloat) priv->width;
    const gfloat f_height = (gfloat) priv->height;

    for (guint i = 0; i < priv->num_balls; i++) {
        const guint x = 2*i, y = 2*i + 1;
        priv->sizes[i] = (gfloat) g_random_double_range(f_width / 50.0f, f_width / 10.0f);
        priv->positions[x] = (gfloat) g_random_double_range(0.0, (double) f_width);
        priv->positions[y] = (gfloat) g_random_double_range(0.0, (double) f_height);
        priv->velocities[x] = (gfloat) g_random_double_range(-4.0, 4.0);
        priv->velocities[y] = (gfloat) g_random_double_range(-4.0, 4.0);
    };

    cl_int err = CL_SUCCESS;
    priv->positions_mem = clCreateBuffer(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            priv->num_position_bytes, priv->positions, &err);
    CHECK_OPENCL_ERROR(err);

    priv->sizes_mem = clCreateBuffer(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            num_sizes_bytes, priv->sizes, &err);
    CHECK_OPENCL_ERROR(err);

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), (void *) &priv->positions_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 2, sizeof(cl_mem), (void *) &priv->sizes_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 3, sizeof(cl_uint), &priv->num_balls));
}

static gboolean
ufo_filter_meta_balls_generate(UfoFilterSource *filter, UfoBuffer *results[], gpointer cmd_queue, GError **error)
{
    UfoFilterMetaBallsPrivate *priv = UFO_FILTER_META_BALLS_GET_PRIVATE(filter);

    if (!priv->run_infinitely && (priv->current_iteration++) >= priv->num_iterations)
        return FALSE;

    cl_mem output_mem = (cl_mem) ufo_buffer_get_device_array(results[0], (cl_command_queue) cmd_queue);
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), (void *) &output_mem));

    CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel((cl_command_queue) cmd_queue, priv->kernel,
            2, NULL, priv->global_work_size, NULL,
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

    CHECK_OPENCL_ERROR(clEnqueueWriteBuffer((cl_command_queue) cmd_queue,
            priv->positions_mem, CL_FALSE,
            0, priv->num_position_bytes, priv->positions,
            0, NULL, NULL));

    g_timer_stop(priv->timer);

    if (priv->frames_per_second > 0) {
        const gdouble elapsed = g_timer_elapsed(priv->timer, NULL);
        const gdouble delta = priv->seconds_per_frame - elapsed;
        if (delta > 0.0)
            g_usleep(G_USEC_PER_SEC * ((gulong) delta));
    }

    return TRUE;
}

static void
ufo_filter_meta_balls_finalize(GObject *object)
{
    UfoFilterMetaBallsPrivate *priv = UFO_FILTER_META_BALLS_GET_PRIVATE(object);

    CHECK_OPENCL_ERROR (clReleaseMemObject(priv->positions_mem));
    CHECK_OPENCL_ERROR (clReleaseMemObject(priv->sizes_mem));
    g_free (priv->sizes);
    g_free (priv->positions);
    g_free (priv->velocities);
    g_timer_destroy (priv->timer);
    G_OBJECT_CLASS (ufo_filter_meta_balls_parent_class)->finalize (object);
}

static void
ufo_filter_meta_balls_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterMetaBallsPrivate *priv = UFO_FILTER_META_BALLS_GET_PRIVATE(object);

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
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_meta_balls_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterMetaBallsPrivate *priv = UFO_FILTER_META_BALLS_GET_PRIVATE(object);

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
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_meta_balls_class_init(UfoFilterMetaBallsClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterSourceClass *filter_class = UFO_FILTER_SOURCE_CLASS(klass);

    gobject_class->set_property = ufo_filter_meta_balls_set_property;
    gobject_class->get_property = ufo_filter_meta_balls_get_property;
    gobject_class->finalize = ufo_filter_meta_balls_finalize;
    filter_class->initialize = ufo_filter_meta_balls_initialize;
    filter_class->generate = ufo_filter_meta_balls_generate;

    meta_balls_properties[PROP_WIDTH] =
        g_param_spec_uint("width",
                "Width of the output",
                "Width of the output",
                1, 8192, 512,
                G_PARAM_READWRITE);

    meta_balls_properties[PROP_HEIGHT] =
        g_param_spec_uint("height",
                "Height of the output",
                "Height of the output",
                1, 8192, 512,
                G_PARAM_READWRITE);

    meta_balls_properties[PROP_NUM_BALLS] =
        g_param_spec_uint("num-balls",
                "Number of meta balls",
                "Number of meta balls",
                1, 256, 1,
                G_PARAM_READWRITE);

    meta_balls_properties[PROP_NUM_ITERATIONS] =
        g_param_spec_uint("num-iterations",
                "Number of iterations",
                "Number of iterations",
                1, G_MAXUINT, 1,
                G_PARAM_READWRITE);

    meta_balls_properties[PROP_RUN_INFINITELY] =
        g_param_spec_boolean("run-infinitely",
                "Run infinitely",
                "Run infinitely",
                FALSE,
                G_PARAM_READWRITE);

    meta_balls_properties[PROP_FRAMES_PER_SECOND] =
        g_param_spec_uint("frames-per-second",
                "Number of frames per second (0 for maximum possible rate)",
                "Number of frames per second (0 for maximum possible rate)",
                0, G_MAXINT, 0,
                G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_WIDTH, meta_balls_properties[PROP_WIDTH]);
    g_object_class_install_property(gobject_class, PROP_HEIGHT, meta_balls_properties[PROP_HEIGHT]);
    g_object_class_install_property(gobject_class, PROP_NUM_BALLS, meta_balls_properties[PROP_NUM_BALLS]);
    g_object_class_install_property(gobject_class, PROP_NUM_ITERATIONS, meta_balls_properties[PROP_NUM_ITERATIONS]);
    g_object_class_install_property(gobject_class, PROP_RUN_INFINITELY, meta_balls_properties[PROP_RUN_INFINITELY]);
    g_object_class_install_property(gobject_class, PROP_FRAMES_PER_SECOND, meta_balls_properties[PROP_FRAMES_PER_SECOND]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterMetaBallsPrivate));
}

static void
ufo_filter_meta_balls_init(UfoFilterMetaBalls *self)
{
    UfoFilterMetaBallsPrivate *priv = self->priv = UFO_FILTER_META_BALLS_GET_PRIVATE(self);
    UfoOutputParameter output_params[] = {{2}};

    priv->width = 512;
    priv->height = 512;
    priv->num_balls = 1;
    priv->num_iterations = 1;
    priv->run_infinitely = FALSE;
    priv->frames_per_second = 0;

    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_META_BALLS, NULL);
}
