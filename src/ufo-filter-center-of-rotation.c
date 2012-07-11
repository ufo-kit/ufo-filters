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
 * Computes the center-of-rotation by registrating 1D projections in a sinogram
 * that are spaced apart in a semi-circle.
 */

struct _UfoFilterCenterOfRotationPrivate {
    gfloat      angle_step;
    gdouble     center;
};

G_DEFINE_TYPE(UfoFilterCenterOfRotation, ufo_filter_center_of_rotation, UFO_TYPE_FILTER)

#define UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_CENTER_OF_ROTATION, UfoFilterCenterOfRotationPrivate))

enum {
    PROP_0,
    PROP_ANGLE_STEP,
    PROP_CENTER,
    N_PROPERTIES
};

static GParamSpec *center_of_rotation_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_center_of_rotation_process_cpu (UfoFilter *filter, UfoBuffer *params[], UfoBuffer *results[], gpointer cmd_queue, GError **error)
{
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
    UfoFilterCenterOfRotationPrivate *priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE (filter);
    guint width, height;

    UfoBuffer *sinogram = params[0];

    ufo_buffer_get_2d_dimensions (sinogram, &width, &height);

    gfloat *proj_0 = ufo_buffer_get_host_array (sinogram, (cl_command_queue) cmd_queue);
    gfloat *proj_180 = proj_0 + (height-1) * width;

    const guint max_displacement = width / 2;
    const guint N = max_displacement * 2 - 1;
    gfloat *scores = g_malloc0 (N * sizeof(float));

    for (gint displacement = (-((gint) max_displacement) + 1); displacement < 0; displacement++) {
        const guint index = (guint) displacement + max_displacement - 1;
        const guint max_x = width - ((guint) ABS (displacement));
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
    g_object_notify (G_OBJECT (filter), "center");
    g_free (scores);
}

static void
ufo_filter_center_of_rotation_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterCenterOfRotationPrivate *priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_ANGLE_STEP:
            priv->angle_step = g_value_get_float(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_center_of_rotation_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterCenterOfRotationPrivate *priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE(object);

    switch (property_id) {
        case PROP_ANGLE_STEP:
            g_value_set_float(value, priv->angle_step);
            break;
        case PROP_CENTER:
            g_value_set_double(value, priv->center);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_center_of_rotation_class_init(UfoFilterCenterOfRotationClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_center_of_rotation_set_property;
    gobject_class->get_property = ufo_filter_center_of_rotation_get_property;
    filter_class->process_cpu = ufo_filter_center_of_rotation_process_cpu;

    center_of_rotation_properties[PROP_ANGLE_STEP] =
        g_param_spec_float("angle-step",
            "Step between two successive projections",
            "Step between two successive projections",
            0.00001f, 180.0f, 1.0f,
            G_PARAM_READWRITE);

    center_of_rotation_properties[PROP_CENTER] =
        g_param_spec_double("center",
            "Center of rotation",
            "The calculated center of rotation",
            -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
            G_PARAM_READABLE);

    g_object_class_install_property (gobject_class, PROP_ANGLE_STEP, center_of_rotation_properties[PROP_ANGLE_STEP]);
    g_object_class_install_property (gobject_class, PROP_CENTER, center_of_rotation_properties[PROP_CENTER]);

    g_type_class_add_private (gobject_class, sizeof(UfoFilterCenterOfRotationPrivate));
}

static void
ufo_filter_center_of_rotation_init(UfoFilterCenterOfRotation *self)
{
    UfoFilterCenterOfRotationPrivate *priv = self->priv = UFO_FILTER_CENTER_OF_ROTATION_GET_PRIVATE (self);
    UfoInputParameter input_params[] = {{2, UFO_FILTER_INFINITE_INPUT}};

    priv->angle_step = 1.0f;

    ufo_filter_register_inputs (UFO_FILTER (self), 1, input_params);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new (UFO_TYPE_FILTER_CENTER_OF_ROTATION, NULL);
}
