#ifndef __UFO_FILTER_HISTOGRAM_THRESHOLD_H
#define __UFO_FILTER_HISTOGRAM_THRESHOLD_H

#include <glib.h>
#include <glib-object.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD             (ufo_filter_histogram_threshold_get_type())
#define UFO_FILTER_HISTOGRAM_THRESHOLD(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD, UfoFilterHistogramThreshold))
#define UFO_IS_FILTER_HISTOGRAM_THRESHOLD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD))
#define UFO_FILTER_HISTOGRAM_THRESHOLD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD, UfoFilterHistogramThresholdClass))
#define UFO_IS_FILTER_HISTOGRAM_THRESHOLD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD))
#define UFO_FILTER_HISTOGRAM_THRESHOLD_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_HISTOGRAM_THRESHOLD, UfoFilterHistogramThresholdClass))

typedef struct _UfoFilterHistogramThreshold           UfoFilterHistogramThreshold;
typedef struct _UfoFilterHistogramThresholdClass      UfoFilterHistogramThresholdClass;
typedef struct _UfoFilterHistogramThresholdPrivate    UfoFilterHistogramThresholdPrivate;

struct _UfoFilterHistogramThreshold {
    /*< private >*/
    UfoFilter parent_instance;

    UfoFilterHistogramThresholdPrivate *priv;
};

/**
 * UfoFilterHistogramThresholdClass:
 *
 * #UfoFilterHistogramThreshold class
 */
struct _UfoFilterHistogramThresholdClass {
    /*< private >*/
    UfoFilterClass parent_class;
};

GType ufo_filter_histogram_threshold_get_type(void);

#endif
