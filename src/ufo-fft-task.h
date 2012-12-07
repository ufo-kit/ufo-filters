#ifndef __UFO_FFT_TASK_H
#define __UFO_FFT_TASK_H

#include <ufo-task-node.h>

G_BEGIN_DECLS

#define UFO_TYPE_FFT_TASK             (ufo_fft_task_get_type())
#define UFO_FFT_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FFT_TASK, UfoFftTask))
#define UFO_IS_FFT_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FFT_TASK))
#define UFO_FFT_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FFT_TASK, UfoFftTaskClass))
#define UFO_IS_FFT_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FFT_TASK))
#define UFO_FFT_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FFT_TASK, UfoFftTaskClass))

typedef struct _UfoFftTask           UfoFftTask;
typedef struct _UfoFftTaskClass      UfoFftTaskClass;
typedef struct _UfoFftTaskPrivate    UfoFftTaskPrivate;

/**
 * UfoFftTask:
 *
 * Main object for organizing filters. The contents of the #UfoFftTask structure
 * are private and should only be accessed via the provided API.
 */
struct _UfoFftTask {
    /*< private >*/
    UfoTaskNode parent_instance;

    UfoFftTaskPrivate *priv;
};

/**
 * UfoFftTaskClass:
 *
 * #UfoFftTask class
 */
struct _UfoFftTaskClass {
    /*< private >*/
    UfoTaskNodeClass parent_class;
};

UfoNode  *ufo_fft_task_new       (void);
GType     ufo_fft_task_get_type  (void);

G_END_DECLS

#endif
