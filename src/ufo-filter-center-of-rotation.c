#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-center-of-rotation.h"

/**
 * SECTION:ufo-filter-center-of-rotation
 * @Short_description: Computer the center-of-rotation
 * @Title: centerofrotation
 *
 * Computes the center-of-rotation by registrating projections that are space
 * apart in a semi-circle.
 */

struct _UfoFilterCenterOfRotationPrivate {
    gboolean use_sinograms; /**< FIXME: we should get this information from the buffer */
    gfloat angle_step;
    gdouble center;
};

G_DEFINE_TYPE(UfoFilterCenterOfRotation, ufo_filter_center_of_rotation, UFO_TYPE_FILTER)

#define UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_CENTER_OF_ROTATION, UfoFilterCenterOfRotationPrivate))

enum {
    PROP_0,
    PROP_ANGLE_STEP,
    PROP_USE_SINOGRAMS,
    PROP_CENTER,
    N_PROPERTIES
};

static GParamSpec *center_of_rotation_properties[N_PROPERTIES] = { NULL, };

static void center_of_rotation_sinograms(UfoFilter *filter)
{
    UfoFilterCenterOfRotationPrivate *priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(filter);
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    UfoBuffer *sinogram = ufo_channel_get_input_buffer(input_channel);
    guint width, height;

    while (sinogram != NULL) {
        ufo_buffer_get_2d_dimensions(sinogram, &width, &height);

        gfloat *proj_0 = ufo_buffer_get_host_array(sinogram, command_queue);
        gfloat *proj_180 = proj_0 + (height-1) * width;

        const guint max_displacement = width / 2;
        const guint N = max_displacement * 2 - 1;
        gfloat *scores = g_malloc0(N * sizeof(float));

        for (gint displacement = (-((gint) max_displacement) + 1); displacement < 0; displacement++) {
            const guint index = (guint) displacement + max_displacement - 1;
            const guint max_x = width - ((guint) ABS(displacement));
            for (guint x = 0; x < max_x; x++) {
                gfloat diff = proj_0[x] - proj_180[(max_x - x + 1)];    
                scores[index] += diff * diff;
            }
        }

        for (guint displacement = 0; displacement < max_displacement; displacement++) {
            const guint index = displacement + max_displacement - 1; 
            for (guint x = 0; x < width-displacement; x++) {
                gfloat diff = proj_0[x+displacement] - proj_180[(width-x+1)];    
                scores[index] += diff * diff;
            }
        }
        
        guint score_index = 0;
        gfloat min_score = scores[0];

        for (guint i = 1; i < N; i++) {
            if (scores[i] < min_score) {
                score_index = i;
                min_score = scores[i];
            } 
        }

        priv->center = (width + score_index - max_displacement + 1) / 2.0;
        /* g_debug("Center of Rotation: %f", priv->center); */
        g_object_notify(G_OBJECT(filter), "center");
        g_free(scores);

        ufo_channel_finalize_input_buffer(input_channel, sinogram);
        sinogram = ufo_channel_get_input_buffer(input_channel);
    }
}

static void center_of_rotation_projections(UfoFilter *filter)
{
    UfoFilterCenterOfRotationPrivate *priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(filter);
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);

    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);
    /* FIXME: we need to copy this to private memory */
    float *proj_0 = ufo_buffer_get_host_array(input, command_queue);
    float *proj_180 = NULL;
    int counter = 0;

    /* Take all buffers until we got the opposite projection */
    ufo_channel_finalize_input_buffer(input_channel, input);
    input = ufo_channel_get_input_buffer(input_channel);
    while (input != NULL) {
        if (ABS((((gfloat) counter++) * priv->angle_step) - 180.0f) < 0.001f) {
            proj_180 = ufo_buffer_get_host_array(input, command_queue); 
            break;
        }
        ufo_channel_finalize_input_buffer(input_channel, input);
        input = ufo_channel_get_input_buffer(input_channel);
    }

    guint width, height;
    ufo_buffer_get_2d_dimensions(input, &width, &height);

    /* We have basically two parameters for tuning the performance: decreasing
     * max_displacement and not considering the whole images but just some of
     * the lines */
    const guint max_displacement = width / 2;
    const guint N = max_displacement * 2 - 1;
    gfloat *scores = g_malloc0(N * sizeof(gfloat));
    gfloat *grad = g_malloc0(N * sizeof(gfloat));

    for (gint displacement = (-((gint) max_displacement) + 1); displacement < 0; displacement++) {
        const guint index = (guint) displacement + max_displacement - 1;
        for (guint y = 0; y < height; y++) {
            const guint max_x = width - ((guint) ABS(displacement));
            for (guint x = 0; x < max_x; x++) {
                gfloat diff = proj_0[y*width+x] - proj_180[y*width + (max_x - x + 1)];    
                scores[index] += diff * diff;
            }
        }
    }

    for (guint displacement = 0; displacement < max_displacement; displacement++) {
        const guint index = displacement + max_displacement - 1; 
        for (guint y = 0; y < height; y++) {
            for (guint x = 0; x < width-displacement; x++) {
                gfloat diff = proj_0[y*width+x+displacement] - proj_180[y*width + (width-x+1)];    
                scores[index] += diff * diff;
            }
        }
    }

    grad[0] = 0.0;

    for (guint i = 1; i < N; i++)
        grad[i] = scores[i] - scores[i-1];

    /* Find local minima. Actually, if max_displacement is not to large (like
     * width/2) the global maximum is always the correct maximum. */
    for (guint i = 1; i < N; i++) {
        if (grad[i-1] < 0.0 && grad[i] > 0.0) {
            priv->center = (width + i - max_displacement + 1) / 2.0;
            g_debug("Local minimum at %f: %f", priv->center, scores[i]);
        }
    }

    g_free(grad);
    g_free(scores);
}

static void ufo_filter_center_of_rotation_initialize(UfoFilter *filter)
{
}

/*
 * This is the main method in which the filter processes one buffer after
 * another.
 */
static void ufo_filter_center_of_rotation_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));

    /* Calculate the principial horizontal displacement according to "Image
     * processing pipeline for synchrotron-radiation-based tomographic
     * microscopy" by C. Hintermüller et al. (2010, International Union of
     * Crystallography, Singapore).
     *
     * In the case of projections, the whole projection at angle 0 and 180 are
     * used for determination of the center of rotation. When using sinograms,
     * we can use the first and last row of the sinogram to determine a center
     * of rotation, which will be most likely worse than those for projections.
     */
    if (UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(filter)->use_sinograms)
        center_of_rotation_sinograms(filter);
    else
        center_of_rotation_projections(filter);
}

static void ufo_filter_center_of_rotation_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterCenterOfRotationPrivate *priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_ANGLE_STEP:
            priv->angle_step = g_value_get_float(value);
            break;
        case PROP_USE_SINOGRAMS:
            priv->use_sinograms = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_center_of_rotation_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterCenterOfRotationPrivate *priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_ANGLE_STEP:
            g_value_set_float(value, priv->angle_step);
            break;
        case PROP_USE_SINOGRAMS:
            g_value_set_boolean(value, priv->use_sinograms);
            break;
        case PROP_CENTER:
            g_value_set_double(value, priv->center);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_center_of_rotation_class_init(UfoFilterCenterOfRotationClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_center_of_rotation_set_property;
    gobject_class->get_property = ufo_filter_center_of_rotation_get_property;
    filter_class->initialize = ufo_filter_center_of_rotation_initialize;
    filter_class->process = ufo_filter_center_of_rotation_process;

    center_of_rotation_properties[PROP_ANGLE_STEP] = 
        g_param_spec_float("angle-step",
            "Step between two successive projections",
            "Step between two successive projections",
            0.00001f, 180.0f, 1.0f,
            G_PARAM_READWRITE);

    center_of_rotation_properties[PROP_USE_SINOGRAMS] = 
        g_param_spec_boolean("use-sinograms",
            "Use sinograms instead of projections",
            "Use sinograms instead of projections",
            FALSE,
            G_PARAM_READWRITE);

    center_of_rotation_properties[PROP_CENTER] = 
        g_param_spec_double("center",
            "Center of rotation",
            "The calculated center of rotation",
            -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
            G_PARAM_READABLE);


    g_object_class_install_property(gobject_class, PROP_ANGLE_STEP, center_of_rotation_properties[PROP_ANGLE_STEP]);
    g_object_class_install_property(gobject_class, PROP_USE_SINOGRAMS, center_of_rotation_properties[PROP_USE_SINOGRAMS]);
    g_object_class_install_property(gobject_class, PROP_CENTER, center_of_rotation_properties[PROP_CENTER]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterCenterOfRotationPrivate));
}

static void ufo_filter_center_of_rotation_init(UfoFilterCenterOfRotation *self)
{
    UfoFilterCenterOfRotationPrivate *priv = self->priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(self);
    priv->angle_step = 1.0f;
    priv->use_sinograms = FALSE;

    ufo_filter_register_input(UFO_FILTER(self), "image", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_CENTER_OF_ROTATION, NULL);
}
