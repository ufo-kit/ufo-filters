#ifndef __UFO_FILTER_CV_SHOW_H
#define __UFO_FILTER_CV_SHOW_H

#include <glib.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_CV_SHOW             (ufo_filter_cv_show_get_type())
#define UFO_FILTER_CV_SHOW(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_CV_SHOW, UfoFilterCvShow))
#define UFO_IS_FILTER_CV_SHOW(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_CV_SHOW))
#define UFO_FILTER_CV_SHOW_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_CV_SHOW, UfoFilterCvShowClass))
#define UFO_IS_FILTER_CV_SHOW_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_CV_SHOW))
#define UFO_FILTER_CV_SHOW_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_CV_SHOW, UfoFilterCvShowClass))

typedef struct _UfoFilterCvShow           UfoFilterCvShow;
typedef struct _UfoFilterCvShowClass      UfoFilterCvShowClass;
typedef struct _UfoFilterCvShowPrivate    UfoFilterCvShowPrivate;

struct _UfoFilterCvShow {
    /*< private >*/
    UfoFilter parent_instance;

    UfoFilterCvShowPrivate *priv;
};

/**
 * UfoFilterCvShowClass:
 *
 * #UfoFilterCvShow class
 */
struct _UfoFilterCvShowClass {
    /*< private >*/
    UfoFilterClass parent_class;
};

GType ufo_filter_cv_show_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
