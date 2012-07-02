#ifndef __UFO_FILTER_NULL_H
#define __UFO_FILTER_NULL_H

#include <glib.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-filter-sink.h>

#define UFO_TYPE_FILTER_NULL             (ufo_filter_null_get_type())
#define UFO_FILTER_NULL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_NULL, UfoFilterNull))
#define UFO_IS_FILTER_NULL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_NULL))
#define UFO_FILTER_NULL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_NULL, UfoFilterNullClass))
#define UFO_IS_FILTER_NULL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_NULL))
#define UFO_FILTER_NULL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_NULL, UfoFilterNullClass))

typedef struct _UfoFilterNull           UfoFilterNull;
typedef struct _UfoFilterNullClass      UfoFilterNullClass;
typedef struct _UfoFilterNullPrivate    UfoFilterNullPrivate;

struct _UfoFilterNull {
    /*< private >*/
    UfoFilterSink parent_instance;

    UfoFilterNullPrivate *priv;
};

/**
 * UfoFilterNullClass:
 *
 * #UfoFilterNull class
 */
struct _UfoFilterNullClass {
    /*< private >*/
    UfoFilterSinkClass parent_class;
};

GType ufo_filter_null_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
