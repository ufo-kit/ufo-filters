#ifndef __UFO_OPENCL_TASK_H
#define __UFO_OPENCL_TASK_H

#include <ufo-task-node.h>

G_BEGIN_DECLS

#define UFO_TYPE_OPENCL_TASK             (ufo_opencl_task_get_type())
#define UFO_OPENCL_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_OPENCL_TASK, UfoOpenCLTask))
#define UFO_IS_OPENCL_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_OPENCL_TASK))
#define UFO_OPENCL_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_OPENCL_TASK, UfoOpenCLTaskClass))
#define UFO_IS_OPENCL_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_OPENCL_TASK))
#define UFO_OPENCL_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_OPENCL_TASK, UfoOpenCLTaskClass))

typedef struct _UfoOpenCLTask           UfoOpenCLTask;
typedef struct _UfoOpenCLTaskClass      UfoOpenCLTaskClass;
typedef struct _UfoOpenCLTaskPrivate    UfoOpenCLTaskPrivate;

/**
 * UfoOpenCLTask:
 *
 * Main object for organizing filters. The contents of the #UfoOpenCLTask structure
 * are private and should only be accessed via the provided API.
 */
struct _UfoOpenCLTask {
    /*< private >*/
    UfoTaskNode parent_instance;

    UfoOpenCLTaskPrivate *priv;
};

/**
 * UfoOpenCLTaskClass:
 *
 * #UfoOpenCLTask class
 */
struct _UfoOpenCLTaskClass {
    /*< private >*/
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_opencl_task_new       (void);
GType     ufo_opencl_task_get_type  (void);

G_END_DECLS

#endif
