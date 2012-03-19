#ifndef __UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE_H
#define __UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE_H

#include <glib.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_OPTICAL_FLOW_LUCAS_KANADE             (ufo_filter_optical_flow_lucas_kanade_get_type())
#define UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_OPTICAL_FLOW_LUCAS_KANADE, UfoFilterOpticalFlowLucasKanade))
#define UFO_IS_FILTER_OPTICAL_FLOW_LUCAS_KANADE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_OPTICAL_FLOW_LUCAS_KANADE))
#define UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_OPTICAL_FLOW_LUCAS_KANADE, UfoFilterOpticalFlowLucasKanadeClass))
#define UFO_IS_FILTER_OPTICAL_FLOW_LUCAS_KANADE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_OPTICAL_FLOW_LUCAS_KANADE))
#define UFO_FILTER_OPTICAL_FLOW_LUCAS_KANADE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_OPTICAL_FLOW_LUCAS_KANADE, UfoFilterOpticalFlowLucasKanadeClass))

typedef struct _UfoFilterOpticalFlowLucasKanade           UfoFilterOpticalFlowLucasKanade;
typedef struct _UfoFilterOpticalFlowLucasKanadeClass      UfoFilterOpticalFlowLucasKanadeClass;
typedef struct _UfoFilterOpticalFlowLucasKanadePrivate    UfoFilterOpticalFlowLucasKanadePrivate;

struct _UfoFilterOpticalFlowLucasKanade {
    /*< private >*/
    UfoFilter parent_instance;

    UfoFilterOpticalFlowLucasKanadePrivate *priv;
};

/**
 * UfoFilterOpticalFlowLucasKanadeClass:
 *
 * #UfoFilterOpticalFlowLucasKanade class
 */
struct _UfoFilterOpticalFlowLucasKanadeClass {
    /*< private >*/
    UfoFilterClass parent_class;
};

GType ufo_filter_optical_flow_lucas_kanade_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
