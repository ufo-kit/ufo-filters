#include <gmodule.h>
#include <math.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include <ufo/ufo-resource-manager.h>

#include "ufo-filter-backproject.h"

/**
 * SECTION:ufo-filter-backproject
 * @Short_description: Back-project incoming sinograms
 * @Title: backproject
 *
 * Project all 1-dimensional projections from a sinogram back into space to
 * compute a slice. This is most likely only useful for filtered sinograms. In
 * case you have unfiltered projections you can filter them by chaining 
 * #UfoFilterFFT, #UfoFilterFilter and #UfoFilterIFFT together.
 */

struct _UfoFilterBackprojectPrivate {
    cl_kernel kernel;
    cl_mem cos_mem;
    cl_mem sin_mem;
    cl_mem axes_mem;
    cl_mem texture;
    gint num_sinograms;
    guint num_projections;
    guint width;
    guint height;
    float axis_position;
    float angle_step;
    gboolean use_texture;
    size_t global_work_size[2];

    float offset_x;
    float offset_y;
};

G_DEFINE_TYPE(UfoFilterBackproject, ufo_filter_backproject, UFO_TYPE_FILTER)

#define UFO_FILTER_BACKPROJECT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_BACKPROJECT, UfoFilterBackprojectPrivate))

enum {
    PROP_0 = 0,
    PROP_AXIS_POSITION,
    PROP_ANGLE_STEP,
    PROP_NUM_SINOGRAMS,
    PROP_NUM_PROJECTIONS,
    PROP_USE_TEXTURE,
    N_PROPERTIES
};

static GParamSpec *
backproject_properties[N_PROPERTIES] = { NULL, };

static gboolean 
axis_is_positive(GValue *value, gpointer user_data)
{
    return g_value_get_double(value) > 0.0;
}

static GError *
ufo_filter_backproject_initialize(UfoFilter *filter, UfoBuffer *params[], guint **dims)
{
    UfoFilterBackprojectPrivate *priv = UFO_FILTER_BACKPROJECT_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    
    ufo_filter_wait_until(filter, backproject_properties[PROP_AXIS_POSITION], &axis_is_positive, NULL);

    if (priv->use_texture)
        priv->kernel = ufo_resource_manager_get_kernel(manager, "backproject.cl", "backproject_tex", &error);
    else
        priv->kernel = ufo_resource_manager_get_kernel(manager, "backproject.cl", "backproject", &error);

    if (error != NULL) 
        return error;

    cl_int errcode = CL_SUCCESS;
    ufo_buffer_get_2d_dimensions(params[0], &priv->width, &priv->height);
    priv->num_projections = priv->num_projections == 0 ? priv->height : MIN(priv->height, priv->num_projections);
    priv->global_work_size[0] = priv->width;
    priv->global_work_size[1] = priv->width;
    dims[0][0] = priv->width;
    dims[0][1] = priv->width;

    float *cos_tmp = g_malloc0(sizeof(float) * priv->num_projections);
    float *sin_tmp = g_malloc0(sizeof(float) * priv->num_projections);
    float *axes_tmp = g_malloc0(sizeof(float) * priv->num_projections);

    float step = priv->angle_step;
    for (guint i = 0; i < priv->num_projections; i++) {
        cos_tmp[i] = (gfloat) cos((gfloat) i*step);
        sin_tmp[i] = (gfloat) sin((gfloat) i*step);
        axes_tmp[i] = priv->axis_position;
    }

    priv->offset_x = -priv->axis_position;
    priv->offset_y = -priv->axis_position;

    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    cl_mem_flags flags = CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR;
    priv->cos_mem = clCreateBuffer(context, flags, sizeof(float) * priv->num_projections, cos_tmp, NULL);
    priv->sin_mem = clCreateBuffer(context, flags, sizeof(float) * priv->num_projections, sin_tmp, NULL);
    priv->axes_mem = clCreateBuffer(context, flags, sizeof(float) * priv->num_projections, axes_tmp, NULL);

    g_free(cos_tmp);
    g_free(sin_tmp);
    g_free(axes_tmp);

    if (priv->use_texture) {
        cl_image_format image_format;
        image_format.image_channel_order = CL_R;
        image_format.image_channel_data_type = CL_FLOAT;
        priv->texture = clCreateImage2D(context, CL_MEM_READ_ONLY,
                &image_format, priv->width, priv->num_projections, 
                0, NULL, &errcode);
    }

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 0, sizeof(gint32), &priv->num_projections));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 1, sizeof(gint32), &priv->width));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 2, sizeof(gint32), &priv->offset_x));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 3, sizeof(gint32), &priv->offset_y));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 4, sizeof(cl_mem), (void *) &priv->cos_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 5, sizeof(cl_mem), (void *) &priv->sin_mem));
    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 6, sizeof(cl_mem), (void *) &priv->axes_mem));

    return NULL;
}

#define BLOCK_SIZE_X 16
#define BLOCK_SIZE_Y 16

static GError *
ufo_filter_backproject_process_gpu(UfoFilter *filter, UfoBuffer *params[], UfoBuffer *results[], gpointer cmd_queue)
{
    UfoFilterBackprojectPrivate *priv = UFO_FILTER_BACKPROJECT_GET_PRIVATE(filter);

    cl_mem sinogram_mem = (cl_mem) ufo_buffer_get_device_array(params[0], (cl_command_queue) cmd_queue);
    cl_mem slice_mem = (cl_mem) ufo_buffer_get_device_array(results[0], (cl_command_queue) cmd_queue);

    if (priv->use_texture) {
        size_t dest_origin[3] = { 0, 0, 0 };
        size_t dest_region[3] = { priv->width, priv->num_projections, 1 };
        CHECK_OPENCL_ERROR(clEnqueueCopyBufferToImage((cl_command_queue) cmd_queue,
                sinogram_mem, priv->texture, 0, dest_origin, dest_region,
                0, NULL, NULL));
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 7, sizeof(cl_mem), (void *) &priv->texture));
    }
    else
        CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 7, sizeof(cl_mem), (void *) &sinogram_mem));

    CHECK_OPENCL_ERROR(clSetKernelArg(priv->kernel, 8, sizeof(cl_mem), (void *) &slice_mem));

    CHECK_OPENCL_ERROR(clEnqueueNDRangeKernel((cl_command_queue) cmd_queue, priv->kernel,
            2, NULL, priv->global_work_size, NULL,
            0, NULL, NULL));

    return NULL;
}

static void
ufo_filter_backproject_finalize(GObject *object)
{
    UfoFilterBackprojectPrivate *priv = UFO_FILTER_BACKPROJECT_GET_PRIVATE(object);

    if (priv->use_texture)
        CHECK_OPENCL_ERROR(clReleaseMemObject(priv->texture));

    CHECK_OPENCL_ERROR(clReleaseMemObject(priv->cos_mem));
    CHECK_OPENCL_ERROR(clReleaseMemObject(priv->sin_mem));
    CHECK_OPENCL_ERROR(clReleaseMemObject(priv->axes_mem));

    G_OBJECT_CLASS(ufo_filter_backproject_parent_class)->finalize(object);
}

static void
ufo_filter_backproject_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterBackproject *self = UFO_FILTER_BACKPROJECT(object);
    switch (property_id) {
        case PROP_NUM_PROJECTIONS:
            self->priv->num_projections = g_value_get_uint(value);
            break;
        case PROP_AXIS_POSITION:
            self->priv->axis_position = (float) g_value_get_double(value);
            break;
        case PROP_ANGLE_STEP:
            self->priv->angle_step = (float) g_value_get_double(value);
            break;
        case PROP_USE_TEXTURE:
            self->priv->use_texture = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_backproject_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterBackproject *self = UFO_FILTER_BACKPROJECT(object);
    switch (property_id) {
        case PROP_NUM_PROJECTIONS:
            g_value_set_uint(value, self->priv->num_projections);
            break;
        case PROP_NUM_SINOGRAMS:
            g_value_set_int(value, self->priv->num_sinograms);
            break;
        case PROP_AXIS_POSITION:
            g_value_set_double(value, (double) self->priv->axis_position);
            break;
        case PROP_ANGLE_STEP:
            g_value_set_double(value, (double) self->priv->angle_step);
            break;
        case PROP_USE_TEXTURE:
            g_value_set_boolean(value, self->priv->use_texture);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_backproject_class_init(UfoFilterBackprojectClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_backproject_set_property;
    gobject_class->get_property = ufo_filter_backproject_get_property;
    gobject_class->finalize = ufo_filter_backproject_finalize;
    filter_class->initialize = ufo_filter_backproject_initialize;
    filter_class->process_gpu = ufo_filter_backproject_process_gpu;

    backproject_properties[PROP_NUM_SINOGRAMS] = 
        g_param_spec_int("num-sinograms",
            "Number of sinograms",
            "Number of to process",
            -1, 8192, 1,
            G_PARAM_READWRITE);

    backproject_properties[PROP_NUM_PROJECTIONS] = 
        g_param_spec_uint("num-projections",
            "Number of 1D projections to respect (0 to use all projections in a sinogram)",
            "Number of 1D projections to respect (0 to use all projections in a sinogram)",
            0, 8192, 1,
            G_PARAM_READWRITE);

    backproject_properties[PROP_AXIS_POSITION] = 
        g_param_spec_double("axis-pos",
            "Position of rotation axis",
            "Position of rotation axis",
            -1.0, +8192.0, 0.0,
            G_PARAM_READWRITE);

    backproject_properties[PROP_ANGLE_STEP] = 
        g_param_spec_double("angle-step",
            "Increment of angle in radians",
            "Increment of angle in radians",
            -4.0 * G_PI, +4.0 * G_PI, 0.0,
            G_PARAM_READWRITE);

    backproject_properties[PROP_USE_TEXTURE] = 
        g_param_spec_boolean("use-texture",
            "Use texture instead of array lookup",
            "Use texture instead of array lookup",
            FALSE,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_NUM_PROJECTIONS, backproject_properties[PROP_NUM_PROJECTIONS]);
    g_object_class_install_property(gobject_class, PROP_NUM_SINOGRAMS, backproject_properties[PROP_NUM_SINOGRAMS]);
    g_object_class_install_property(gobject_class, PROP_AXIS_POSITION, backproject_properties[PROP_AXIS_POSITION]);
    g_object_class_install_property(gobject_class, PROP_ANGLE_STEP, backproject_properties[PROP_ANGLE_STEP]);
    g_object_class_install_property(gobject_class, PROP_USE_TEXTURE, backproject_properties[PROP_USE_TEXTURE]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterBackprojectPrivate));
}

static void
ufo_filter_backproject_init(UfoFilterBackproject *self)
{
    self->priv = UFO_FILTER_BACKPROJECT_GET_PRIVATE (self);
    self->priv->num_projections = 0;
    self->priv->use_texture = TRUE;
    self->priv->axis_position = -1.0;

    ufo_filter_register_inputs (UFO_FILTER (self), 2, NULL);
    ufo_filter_register_outputs (UFO_FILTER (self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_BACKPROJECT, NULL);
}
