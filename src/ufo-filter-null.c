#include <gmodule.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-null.h"

/**
 * SECTION:ufo-filter-null
 * @Short_description: Discard input
 * @Title: null
 *
 * This node discards any input similar to what /dev/null provides.
 */

G_DEFINE_TYPE(UfoFilterNull, ufo_filter_null, UFO_TYPE_FILTER)

#define UFO_FILTER_NULL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_NULL, UfoFilterNullPrivate))

static GError *
ufo_filter_null_process_cpu(UfoFilter *filter, UfoBuffer *params[], UfoBuffer *results[], gpointer cmd_queue)
{
    return NULL;
}

static void 
ufo_filter_null_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void 
ufo_filter_null_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    switch (property_id) {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void 
ufo_filter_null_class_init(UfoFilterNullClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_null_set_property;
    gobject_class->get_property = ufo_filter_null_get_property;
    filter_class->process_cpu = ufo_filter_null_process_cpu;
}

static void 
ufo_filter_null_init(UfoFilterNull *self)
{
    ufo_filter_register_inputs(UFO_FILTER(self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_NULL, NULL);
}

