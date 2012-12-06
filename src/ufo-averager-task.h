#ifndef __UFO_AVERAGER_TASK_H
#define __UFO_AVERAGER_TASK_H

#include <ufo-task-node.h>

G_BEGIN_DECLS

#define UFO_TYPE_AVERAGER_TASK             (ufo_averager_task_get_type())
#define UFO_AVERAGER_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_AVERAGER_TASK, UfoAveragerTask))
#define UFO_IS_AVERAGER_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_AVERAGER_TASK))
#define UFO_AVERAGER_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_AVERAGER_TASK, UfoAveragerTaskClass))
#define UFO_IS_AVERAGER_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_AVERAGER_TASK))
#define UFO_AVERAGER_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_AVERAGER_TASK, UfoAveragerTaskClass))

typedef struct _UfoAveragerTask           UfoAveragerTask;
typedef struct _UfoAveragerTaskClass      UfoAveragerTaskClass;
typedef struct _UfoAveragerTaskPrivate    UfoAveragerTaskPrivate;

/**
 * UfoAveragerTask:
 *
 * Main object for organizing filters. The contents of the #UfoAveragerTask structure
 * are private and should only be accessed via the provided API.
 */
struct _UfoAveragerTask {
    /*< private >*/
    UfoTaskNode parent_instance;

    UfoAveragerTaskPrivate *priv;
};

/**
 * UfoAveragerTaskClass:
 *
 * #UfoAveragerTask class
 */
struct _UfoAveragerTaskClass {
    /*< private >*/
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_averager_task_new       (void);
GType     ufo_averager_task_get_type  (void);

G_END_DECLS

#endif
