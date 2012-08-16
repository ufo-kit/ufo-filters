#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <stdio.h>
#include <math.h>
#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>
#include "ufo-filter-volume-renderer.h"

/**
 * SECTION:ufo-filter-volume-renderer
 * @Short_description: Volume rendering
 * @Title: volumerenderer
 *
 * Render volumes with ray-casting.
 */

struct _UfoFilterVolumeRendererPrivate {
    cl_kernel   kernel;
    cl_mem      volume_mem;
    cl_mem      view_mem;
    guint       width;
    guint       height;
    gsize       global_work_size[2];
    guint8     *input_data;
    gfloat      angle;
    gfloat     *view_matrix;
};

G_DEFINE_TYPE(UfoFilterVolumeRenderer, ufo_filter_volume_renderer, UFO_TYPE_FILTER)

#define UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_VOLUME_RENDERER, UfoFilterVolumeRendererPrivate))

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPERTIES
};

static GParamSpec *volume_renderer_properties[N_PROPERTIES] = { NULL, };


static void
ufo_filter_volume_renderer_initialize(UfoFilter *filter, UfoBuffer *params[], guint **dims, GError **error)
{
    UfoFilterVolumeRendererPrivate *priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(filter);
    UfoResourceManager *manager = ufo_filter_get_resource_manager(filter);
    GError *tmp_error = NULL;
    cl_int clerror = CL_SUCCESS;
    cl_context context = (cl_context) ufo_resource_manager_get_context(manager);
    guint width, height, slices;

    width = height = slices = 256;
    priv->kernel = ufo_resource_manager_get_kernel(manager, "volume.cl", "rayCastVolume", &tmp_error);

    if (tmp_error != NULL) {
        g_propagate_error (error, tmp_error);
        return;
    }

    priv->angle = 0.0f;
    priv->input_data = g_malloc0(width * height * slices);

    FILE *fp = fopen("/home/matthias/data/amd-volume/aneurism.raw", "rb");
    gsize read = fread(priv->input_data, 1, width * height * slices, fp);
    fclose(fp);
    /* TODO: return proper error */
    g_assert(read == width * height * slices);

    cl_image_format volume_format = {
        .image_channel_order = CL_LUMINANCE,
        .image_channel_data_type = CL_UNORM_INT8
    };

    priv->view_matrix = g_malloc0(4 * 4 * sizeof(gfloat));
    priv->view_matrix[0] = 1.0f;
    priv->view_matrix[5] = 1.0f;
    priv->view_matrix[10] = 1.0f;
    priv->view_matrix[12] = 0.5f;
    priv->view_matrix[14] = 0.5f;
    priv->view_matrix[15] = 1.0f;

    priv->volume_mem = clCreateImage3D(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            &volume_format,
            width, height, slices,
            0, 0, priv->input_data, &clerror);
    CHECK_OPENCL_ERROR(clerror);

    priv->view_mem = clCreateBuffer(context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            4 * 4 * sizeof(gfloat), priv->view_matrix, &clerror);
    CHECK_OPENCL_ERROR(clerror);

    priv->global_work_size[0] = dims[0][0] = width;
    priv->global_work_size[1] = dims[0][1] = height;

    gfloat step_size = 0.003f;
    gfloat displacement = -0.3f;
    gfloat linear_ramp_slope = 0.1f;
    gfloat linear_ramp_constant = 0.01f;
    gfloat threshold = 0.083f;
    cl_uint steps = (cl_uint) ((1.414f + fabs(displacement)) / step_size);

    clerror |= clSetKernelArg(priv->kernel, 0, sizeof(cl_mem), &priv->volume_mem);
    clerror |= clSetKernelArg(priv->kernel, 2, sizeof(cl_mem), &priv->view_mem);
    clerror |= clSetKernelArg(priv->kernel, 3, sizeof(cl_uint), &steps);
    clerror |= clSetKernelArg(priv->kernel, 4, sizeof(gfloat), &step_size);
    clerror |= clSetKernelArg(priv->kernel, 5, sizeof(gfloat), &displacement);
    clerror |= clSetKernelArg(priv->kernel, 6, sizeof(gfloat), &linear_ramp_slope);
    clerror |= clSetKernelArg(priv->kernel, 7, sizeof(gfloat), &linear_ramp_constant);
    clerror |= clSetKernelArg(priv->kernel, 8, sizeof(gfloat), &threshold);
}

static void
ufo_filter_volume_renderer_process_gpu(UfoFilter *filter, UfoBuffer *inputs[], UfoBuffer *outputs[], gpointer cmd_queue, GError **error)
{
    UfoFilterVolumeRendererPrivate *priv;
    cl_mem output_mem;
    cl_int clerror;

    priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(filter);

    if (priv->angle >= G_PI)
        return;

    output_mem = ufo_buffer_get_device_array(outputs[0], (cl_command_queue) cmd_queue);
    clerror = clSetKernelArg(priv->kernel, 1, sizeof(cl_mem), &output_mem);
    CHECK_OPENCL_ERROR(clerror);

    /* TODO: manage copy event so that we don't have to block here */
    CHECK_OPENCL_ERROR(clEnqueueWriteBuffer((cl_command_queue) cmd_queue,
                       priv->view_mem, CL_TRUE,
                       0, 4 * 4 * sizeof(float), priv->view_matrix,
                       0, NULL, NULL));

    ufo_profiler_call (ufo_filter_get_profiler (filter),
                       cmd_queue, priv->kernel,
                       2, priv->global_work_size, NULL);

    /* rotate around the x-axis for now */
    const gfloat cos_angle = (gfloat) cos(priv->angle);
    const gfloat sin_angle = (gfloat) sin(priv->angle);
    priv->view_matrix[0] = cos_angle;
    priv->view_matrix[2] = sin_angle;
    priv->view_matrix[12] = -sin_angle;
    priv->view_matrix[14] = cos_angle;
    priv->angle += 0.05f;
}

static void
ufo_filter_volume_renderer_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterVolumeRendererPrivate *priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_WIDTH:
            priv->width = g_value_get_uint(value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_volume_renderer_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterVolumeRendererPrivate *priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_WIDTH:
            g_value_set_uint(value, priv->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint(value, priv->height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_volume_renderer_finalize(GObject *object)
{
    UfoFilterVolumeRendererPrivate *priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(object);

    CHECK_OPENCL_ERROR (clReleaseMemObject(priv->volume_mem));
    CHECK_OPENCL_ERROR (clReleaseMemObject(priv->view_mem));
    g_free (priv->view_matrix);
    g_free (priv->input_data);

    G_OBJECT_CLASS (ufo_filter_volume_renderer_parent_class)->finalize (object);
}

static void
ufo_filter_volume_renderer_class_init(UfoFilterVolumeRendererClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_volume_renderer_set_property;
    gobject_class->get_property = ufo_filter_volume_renderer_get_property;
    gobject_class->finalize = ufo_filter_volume_renderer_finalize;
    filter_class->initialize = ufo_filter_volume_renderer_initialize;
    filter_class->process_gpu = ufo_filter_volume_renderer_process_gpu;

    volume_renderer_properties[PROP_WIDTH] =
        g_param_spec_uint("width",
            "Width",
            "Width of the output image",
            1, 8192, 512,
            G_PARAM_READWRITE);

    volume_renderer_properties[PROP_HEIGHT] =
        g_param_spec_uint("height",
            "Height",
            "Height of the output image",
            1, 8192, 512,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_WIDTH, volume_renderer_properties[PROP_WIDTH]);
    g_object_class_install_property(gobject_class, PROP_HEIGHT, volume_renderer_properties[PROP_HEIGHT]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterVolumeRendererPrivate));
}

static void
ufo_filter_volume_renderer_init(UfoFilterVolumeRenderer *self)
{
    UfoOutputParameter output_params[] = {{2}};

    self->priv = UFO_FILTER_VOLUME_RENDERER_GET_PRIVATE(self);
    self->priv->width = 512;
    self->priv->height = 512;

    ufo_filter_register_outputs (UFO_FILTER (self), 1, output_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_VOLUME_RENDERER, NULL);
}
