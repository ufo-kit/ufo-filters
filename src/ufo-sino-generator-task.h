#ifndef __UFO_SINO_GENERATOR_TASK_H
#define __UFO_SINO_GENERATOR_TASK_H

#include <ufo-task-node.h>

G_BEGIN_DECLS

#define UFO_TYPE_SINO_GENERATOR_TASK             (ufo_sino_generator_task_get_type())
#define UFO_SINO_GENERATOR_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_SINO_GENERATOR_TASK, UfoSinoGeneratorTask))
#define UFO_IS_SINO_GENERATOR_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_SINO_GENERATOR_TASK))
#define UFO_SINO_GENERATOR_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_SINO_GENERATOR_TASK, UfoSinoGeneratorTaskClass))
#define UFO_IS_SINO_GENERATOR_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_SINO_GENERATOR_TASK))
#define UFO_SINO_GENERATOR_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_SINO_GENERATOR_TASK, UfoSinoGeneratorTaskClass))

typedef struct _UfoSinoGeneratorTask           UfoSinoGeneratorTask;
typedef struct _UfoSinoGeneratorTaskClass      UfoSinoGeneratorTaskClass;
typedef struct _UfoSinoGeneratorTaskPrivate    UfoSinoGeneratorTaskPrivate;

/**
 * UfoSinoGeneratorTask:
 *
 * Main object for organizing filters. The contents of the #UfoSinoGeneratorTask structure
 * are private and should only be accessed via the provided API.
 */
struct _UfoSinoGeneratorTask {
    /*< private >*/
    UfoTaskNode parent_instance;

    UfoSinoGeneratorTaskPrivate *priv;
};

/**
 * UfoSinoGeneratorTaskClass:
 *
 * #UfoSinoGeneratorTask class
 */
struct _UfoSinoGeneratorTaskClass {
    /*< private >*/
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_sino_generator_task_new       (void);
GType     ufo_sino_generator_task_get_type  (void);

G_END_DECLS

#endif
