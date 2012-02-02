#ifndef __UFO_FILTER_AVERAGER_H
#define __UFO_FILTER_AVERAGER_H

#include <glib.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_AVERAGER             (ufo_filter_averager_get_type())
#define UFO_FILTER_AVERAGER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_AVERAGER, UfoFilterAverager))
#define UFO_IS_FILTER_AVERAGER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_AVERAGER))
#define UFO_FILTER_AVERAGER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_AVERAGER, UfoFilterAveragerClass))
#define UFO_IS_FILTER_AVERAGER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_AVERAGER))
#define UFO_FILTER_AVERAGER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_AVERAGER, UfoFilterAveragerClass))

typedef struct _UfoFilterAverager           UfoFilterAverager;
typedef struct _UfoFilterAveragerClass      UfoFilterAveragerClass;
typedef struct _UfoFilterAveragerPrivate    UfoFilterAveragerPrivate;

struct _UfoFilterAverager {
    UfoFilter parent_instance;

    /* private */
    UfoFilterAveragerPrivate *priv;
};

struct _UfoFilterAveragerClass {
    UfoFilterClass parent_class;
};

/* virtual public methods */

/* non-virtual public methods */

GType ufo_filter_averager_get_type(void);

#endif
