#ifndef __UFO_FILTER_FORWARD_PROJECT_H
#define __UFO_FILTER_FORWARD_PROJECT_H

#include <glib.h>
#include <glib-object.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_FORWARD_PROJECT             (ufo_filter_forward_project_get_type())
#define UFO_FILTER_FORWARD_PROJECT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_FORWARD_PROJECT, UfoFilterForwardProject))
#define UFO_IS_FILTER_FORWARD_PROJECT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_FORWARD_PROJECT))
#define UFO_FILTER_FORWARD_PROJECT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_FORWARD_PROJECT, UfoFilterForwardProjectClass))
#define UFO_IS_FILTER_FORWARD_PROJECT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_FORWARD_PROJECT))
#define UFO_FILTER_FORWARD_PROJECT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_FORWARD_PROJECT, UfoFilterForwardProjectClass))

typedef struct _UfoFilterForwardProject           UfoFilterForwardProject;
typedef struct _UfoFilterForwardProjectClass      UfoFilterForwardProjectClass;
typedef struct _UfoFilterForwardProjectPrivate    UfoFilterForwardProjectPrivate;

struct _UfoFilterForwardProject {
    UfoFilter parent_instance;

    UfoFilterForwardProjectPrivate *priv;
};

struct _UfoFilterForwardProjectClass {
    UfoFilterClass parent_class;
};

GType ufo_filter_forward_project_get_type(void);

#endif
