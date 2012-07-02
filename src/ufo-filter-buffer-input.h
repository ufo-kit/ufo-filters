#ifndef __UFO_FILTER_BUFFER_INPUT_H
#define __UFO_FILTER_BUFFER_INPUT_H

#include <glib.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-filter-source.h>

#define UFO_TYPE_FILTER_BUFFER_INPUT             (ufo_filter_buffer_input_get_type())
#define UFO_FILTER_BUFFER_INPUT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_BUFFER_INPUT, UfoFilterBufferInput))
#define UFO_IS_FILTER_BUFFER_INPUT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_BUFFER_INPUT))
#define UFO_FILTER_BUFFER_INPUT_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_BUFFER_INPUT, UfoFilterBufferInputClass))
#define UFO_IS_FILTER_BUFFER_INPUT_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_BUFFER_INPUT))
#define UFO_FILTER_BUFFER_INPUT_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_BUFFER_INPUT, UfoFilterBufferInputClass))

typedef struct _UfoFilterBufferInput           UfoFilterBufferInput;
typedef struct _UfoFilterBufferInputClass      UfoFilterBufferInputClass;
typedef struct _UfoFilterBufferInputPrivate    UfoFilterBufferInputPrivate;

struct _UfoFilterBufferInput {
    /*< private >*/
    UfoFilterSource parent_instance;

    UfoFilterBufferInputPrivate *priv;
};

/**
 * UfoFilterBufferInputClass:
 *
 * #UfoFilterBufferInput class
 */
struct _UfoFilterBufferInputClass {
    /*< private >*/
    UfoFilterSourceClass parent_class;
};

GType ufo_filter_buffer_input_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
