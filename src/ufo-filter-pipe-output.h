#ifndef __UFO_FILTER_PIPE_OUTPUT_H
#define __UFO_FILTER_PIPE_OUTPUT_H

#include <glib.h>
#include <ufo/ufo-filter-sink.h>

#define UFO_TYPE_FILTER_PIPE_OUTPUT             (ufo_filter_pipe_output_get_type())
#define UFO_FILTER_PIPE_OUTPUT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_PIPE_OUTPUT, UfoFilterPipeOutput))
#define UFO_IS_FILTER_PIPE_OUTPUT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_PIPE_OUTPUT))
#define UFO_FILTER_PIPE_OUTPUT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_PIPE_OUTPUT, UfoFilterPipeOutputClass))
#define UFO_IS_FILTER_PIPE_OUTPUT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_PIPE_OUTPUT))
#define UFO_FILTER_PIPE_OUTPUT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_PIPE_OUTPUT, UfoFilterPipeOutputClass))

typedef struct _UfoFilterPipeOutput           UfoFilterPipeOutput;
typedef struct _UfoFilterPipeOutputClass      UfoFilterPipeOutputClass;
typedef struct _UfoFilterPipeOutputPrivate    UfoFilterPipeOutputPrivate;

struct _UfoFilterPipeOutput {
    /*< private >*/
    UfoFilterSink parent_instance;

    UfoFilterPipeOutputPrivate *priv;
};

/**
 * UfoFilterPipeOutputClass:
 *
 * #UfoFilterPipeOutput class
 */
struct _UfoFilterPipeOutputClass {
    /*< private >*/
    UfoFilterSinkClass parent_class;
};

GType ufo_filter_pipe_output_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
