#ifndef __UFO_FILTER_TASK_H
#define __UFO_FILTER_TASK_H

#include <ufo-task-node.h>

G_BEGIN_DECLS

#define UFO_TYPE_FILTER_TASK             (ufo_filter_task_get_type())
#define UFO_FILTER_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_TASK, UfoFilterTask))
#define UFO_IS_FILTER_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_TASK))
#define UFO_FILTER_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_TASK, UfoFilterTaskClass))
#define UFO_IS_FILTER_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_TASK))
#define UFO_FILTER_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_TASK, UfoFilterTaskClass))

typedef struct _UfoFilterTask           UfoFilterTask;
typedef struct _UfoFilterTaskClass      UfoFilterTaskClass;
typedef struct _UfoFilterTaskPrivate    UfoFilterTaskPrivate;

/**
 * UfoFilterTask:
 *
 * Main object for organizing filters. The contents of the #UfoFilterTask structure
 * are private and should only be accessed via the provided API.
 */
struct _UfoFilterTask {
    /*< private >*/
    UfoTaskNode parent_instance;

    UfoFilterTaskPrivate *priv;
};

/**
 * UfoFilterTaskClass:
 *
 * #UfoFilterTask class
 */
struct _UfoFilterTaskClass {
    /*< private >*/
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_filter_task_new       (void);
GType     ufo_filter_task_get_type  (void);

G_END_DECLS

#endif
