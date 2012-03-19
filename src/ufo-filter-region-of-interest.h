#ifndef __UFO_FILTER_REGION_OF_INTEREST_H
#define __UFO_FILTER_REGION_OF_INTEREST_H

#include <glib.h>
#include <glib-object.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_REGION_OF_INTEREST             (ufo_filter_region_of_interest_get_type())
#define UFO_FILTER_REGION_OF_INTEREST(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_REGION_OF_INTEREST, UfoFilterRegionOfInterest))
#define UFO_IS_FILTER_REGION_OF_INTEREST(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_REGION_OF_INTEREST))
#define UFO_FILTER_REGION_OF_INTEREST_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_REGION_OF_INTEREST, UfoFilterRegionOfInterestClass))
#define UFO_IS_FILTER_REGION_OF_INTEREST_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_REGION_OF_INTEREST))
#define UFO_FILTER_REGION_OF_INTEREST_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_REGION_OF_INTEREST, UfoFilterRegionOfInterestClass))

typedef struct _UfoFilterRegionOfInterest           UfoFilterRegionOfInterest;
typedef struct _UfoFilterRegionOfInterestClass      UfoFilterRegionOfInterestClass;
typedef struct _UfoFilterRegionOfInterestPrivate    UfoFilterRegionOfInterestPrivate;

struct _UfoFilterRegionOfInterest {
    /*< private >*/
    UfoFilter parent_instance;

    UfoFilterRegionOfInterestPrivate *priv;
};

/**
 * UfoFilterRegionOfInterestClass:
 *
 * #UfoFilterRegionOfInterest class
 */
struct _UfoFilterRegionOfInterestClass {
    /*< private >*/
    UfoFilterClass parent_class;
};

GType ufo_filter_region_of_interest_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
