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

struct _UfoFilterMetaBallsPrivate {
    cl_kernel kernel;
    guint width;
    guint height;
    guint num_balls;
    gint num_iterations;
    gboolean run_infinitely;
    guint frames_per_second;
};

GType ufo_filter_meta_balls_get_type(void) G_GNUC_CONST;

G_DEFINE_TYPE(UfoFilterMetaBalls, ufo_filter_meta_balls, UFO_TYPE_FILTER);

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


static void ufo_filter_meta_balls_initialize(UfoFilter *filter)
{
    UfoFilterMetaBallsPrivate *priv = UFO_FILTER_META_BALLS_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;

    ufo_resource_manager_add_program(manager, "metaballs.cl", NULL, &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
        return;
    }

    priv->kernel = ufo_resource_manager_get_kernel(manager, "draw_metaballs", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

static void ufo_filter_meta_balls_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterMetaBallsPrivate *priv = UFO_FILTER_META_BALLS_GET_PRIVATE(filter);

    UfoResourceManager *manager = ufo_resource_manager();
    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);
    cl_int error = CL_SUCCESS;
    cl_kernel kernel = priv->kernel;

    UfoChannel *output_channel = ufo_filter_get_output_channel(filter);
    UfoBuffer *output = NULL;

    const guint dim_size[2] = { priv->width, priv->height };
    const gsize global_work_size[2] = { priv->width, priv->height };
    ufo_channel_allocate_output_buffers(output_channel, 2, dim_size);

    const gsize num_position_bytes = 2 * priv->num_balls * sizeof(float);
    float *positions = g_malloc0(num_position_bytes);
    float *velocities = g_malloc0(num_position_bytes);

    const gsize num_sizes_bytes = priv->num_balls * sizeof(float);
    float *sizes = g_malloc0(num_sizes_bytes);

    for (int i = 0; i < priv->num_balls; i++) {
        const int x = 2*i, y = 2*i + 1;
        sizes[i] = (float) g_random_double_range(priv->width / 50.0f, priv->width / 10.0f);
        positions[x] = (float) g_random_double_range(0.0, (double) priv->width); 
        positions[y] = (float) g_random_double_range(0.0, (double) priv->height); 
        velocities[x] = (float) g_random_double_range(-4.0, 4.0);
        velocities[y] = (float) g_random_double_range(-4.0, 4.0);
    };

    cl_mem positions_mem = clCreateBuffer(context, 
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
            num_position_bytes, positions, &error);
    CHECK_ERROR(error);

    cl_mem sizes_mem = clCreateBuffer(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            num_sizes_bytes, sizes, &error);
    CHECK_ERROR(error);

    CHECK_ERROR(clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *) &positions_mem));
    CHECK_ERROR(clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *) &sizes_mem));
    CHECK_ERROR(clSetKernelArg(kernel, 3, sizeof(cl_uint), &priv->num_balls));

    GTimer *timer = g_timer_new();
    const gdouble seconds_per_frame = 1.0 / ((gdouble) priv->fps);
    int i = 0;

    while (priv->run_infinitely || (i++ < priv->num_iterations)) {
        g_timer_start(timer);
        output = ufo_channel_get_output_buffer(output_channel);
        cl_mem output_mem = (cl_mem) ufo_buffer_get_device_array(output, command_queue);
        CHECK_ERROR(clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *) &output_mem));

        CHECK_ERROR(clEnqueueNDRangeKernel(command_queue, kernel,
                2, NULL, global_work_size, NULL,
                0, NULL, NULL));

        ufo_channel_finalize_output_buffer(output_channel, output);

        /* Update positions and velocities */
        for (guint i = 0; i < priv->num_balls; i++) {
            const int x = 2*i, y = 2*i + 1;
            positions[x] += velocities[x]; 
            positions[y] += velocities[y]; 

            if (positions[x] < 0 || positions[x] > priv->width)
                velocities[x] = -velocities[x];

            if (positions[y] < 0 || positions[y] > priv->height)
                velocities[y] = -velocities[y];
        }

        CHECK_ERROR(clEnqueueWriteBuffer(command_queue,
                positions_mem, CL_FALSE, 
                0, num_position_bytes, positions, 
                0, NULL, NULL));

        g_timer_stop(timer);
        if (priv->fps > 0) {
            const gdouble elapsed = g_timer_elapsed(timer, NULL);
            const gdouble delta = seconds_per_frame - elapsed;
            if (delta > 0.0)
                g_usleep((gulong) G_USEC_PER_SEC * delta); 
        }
    }
    
    ufo_channel_finish(output_channel);

    clReleaseMemObject(positions_mem);
    clReleaseMemObject(sizes_mem);

    g_free(sizes);
    g_free(positions);
    g_free(velocities);
    g_timer_destroy(timer);
}

static void ufo_filter_meta_balls_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
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

static void ufo_filter_meta_balls_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
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

static void ufo_filter_meta_balls_class_init(UfoFilterMetaBallsClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_meta_balls_set_property;
    gobject_class->get_property = ufo_filter_meta_balls_get_property;
    filter_class->initialize = ufo_filter_meta_balls_initialize;
    filter_class->process = ufo_filter_meta_balls_process;

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

static void ufo_filter_meta_balls_init(UfoFilterMetaBalls *self)
{
    UfoFilterMetaBallsPrivate *priv = self->priv = UFO_FILTER_META_BALLS_GET_PRIVATE(self);
    priv->width = 512;
    priv->height = 512;
    priv->num_iterations = 1;
    priv->run_infinitely = FALSE;
    priv->frames_per_second = 0;

    ufo_filter_register_output(UFO_FILTER(self), "image", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_META_BALLS, NULL);
}
