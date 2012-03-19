#ifndef __UFO_FILTER_INTERPOLATOR_H
#define __UFO_FILTER_INTERPOLATOR_H

#include <glib.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_INTERPOLATOR             (ufo_filter_interpolator_get_type())
#define UFO_FILTER_INTERPOLATOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_INTERPOLATOR, UfoFilterInterpolator))
#define UFO_IS_FILTER_INTERPOLATOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_INTERPOLATOR))
#define UFO_FILTER_INTERPOLATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_INTERPOLATOR, UfoFilterInterpolatorClass))
#define UFO_IS_FILTER_INTERPOLATOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_INTERPOLATOR))
#define UFO_FILTER_INTERPOLATOR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_INTERPOLATOR, UfoFilterInterpolatorClass))

typedef struct _UfoFilterInterpolator           UfoFilterInterpolator;
typedef struct _UfoFilterInterpolatorClass      UfoFilterInterpolatorClass;
typedef struct _UfoFilterInterpolatorPrivate    UfoFilterInterpolatorPrivate;

struct _UfoFilterInterpolator {
    /*< private >*/
    UfoFilter parent_instance;

    UfoFilterInterpolatorPrivate *priv;
};

/**
 * UfoFilterInterpolatorClass:
 *
 * #UfoFilterInterpolator class
 */
struct _UfoFilterInterpolatorClass {
    /*< private >*/
    UfoFilterClass parent_class;
};

GType ufo_filter_interpolator_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
