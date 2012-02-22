#ifndef __UFO_FILTER_CAM_ACCESS_H
#define __UFO_FILTER_CAM_ACCESS_H

#include <glib.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_CAM_ACCESS             (ufo_filter_cam_access_get_type())
#define UFO_FILTER_CAM_ACCESS(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_CAM_ACCESS, UfoFilterCamAccess))
#define UFO_IS_FILTER_CAM_ACCESS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_CAM_ACCESS))
#define UFO_FILTER_CAM_ACCESS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_CAM_ACCESS, UfoFilterCamAccessClass))
#define UFO_IS_FILTER_CAM_ACCESS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_CAM_ACCESS))
#define UFO_FILTER_CAM_ACCESS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_CAM_ACCESS, UfoFilterCamAccessClass))

typedef struct _UfoFilterCamAccess           UfoFilterCamAccess;
typedef struct _UfoFilterCamAccessClass      UfoFilterCamAccessClass;
typedef struct _UfoFilterCamAccessPrivate    UfoFilterCamAccessPrivate;

struct _UfoFilterCamAccess {
    UfoFilter parent_instance;

    /* public */

    /* private */
    UfoFilterCamAccessPrivate *priv;
};

/**
 * UfoFilterCamAccessClass:
 *
 * #UfoFilterCamAccess class
 */
struct _UfoFilterCamAccessClass {
    UfoFilterClass parent_class;
};


GType ufo_filter_cam_access_get_type(void);

#endif
