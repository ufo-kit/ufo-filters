#include <gmodule.h>
#include <CL/cl.h>

#include "ufo-filter-backproject.h"
#include "ufo-filter.h"
#include "ufo-element.h"
#include "ufo-buffer.h"
#include "ufo-resource-manager.h"


struct _UfoFilterBackprojectPrivate {
    cl_kernel kernel;
    gint num_sinograms;
};

GType ufo_filter_backproject_get_type(void) G_GNUC_CONST;

/* Inherit from UFO_TYPE_FILTER */
G_DEFINE_TYPE(UfoFilterBackproject, ufo_filter_backproject, UFO_TYPE_FILTER);

#define UFO_FILTER_BACKPROJECT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_BACKPROJECT, UfoFilterBackprojectPrivate))

enum {
    PROP_0 = 0,
    PROP_NUM_SINOGRAMS,
    N_PROPERTIES
};

static GParamSpec *backproject_properties[N_PROPERTIES] = { NULL, };

static void activated(EthosPlugin *plugin)
{
}

static void deactivated(EthosPlugin *plugin)
{
}

/* 
 * virtual methods 
 */
static void ufo_filter_backproject_initialize(UfoFilter *filter)
{
    UfoFilterBackproject *self = UFO_FILTER_BACKPROJECT(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GError *error = NULL;
    self->priv->kernel = NULL;

    ufo_resource_manager_add_program(manager, "backproject.cl", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
        return;
    }

    self->priv->kernel = ufo_resource_manager_get_kernel(manager, "backproject", &error);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_error_free(error);
    }
}

static void ufo_filter_backproject_process(UfoFilter *filter)
{
    g_return_if_fail(UFO_IS_FILTER(filter));
    UfoFilterBackproject *self = UFO_FILTER_BACKPROJECT(filter);
    UfoResourceManager *manager = ufo_resource_manager();
    GAsyncQueue *input_queue = ufo_element_get_input_queue(UFO_ELEMENT(filter));
    GAsyncQueue *output_queue = ufo_element_get_output_queue(UFO_ELEMENT(filter));

    UfoBuffer *sinogram = (UfoBuffer *) g_async_queue_pop(input_queue);
    while (!ufo_buffer_is_finished(sinogram)) {
        if (self->priv->kernel != NULL) {
            gsize global_work_size[2];

            ufo_buffer_get_dimensions(sinogram, 
                    (gint32 *) &global_work_size[0], 
                    (gint32 *) &global_work_size[1]);
            g_message("sinogram dims=%ix%i", (int) global_work_size[0], (int) global_work_size[1]);

            /* TODO: We consume the sinogram and allocate a new buffer for the
             * slice. We should also allocate private buffers for the constant data
             * or put it in like that hack from Suren. */
            /*
            cl_mem buffer_mem = (cl_mem) ufo_buffer_get_gpu_data(sinogram);
            cl_int err = CL_SUCCESS;
            cl_event event;

            err = clSetKernelArg(self->priv->kernel, 1, sizeof(cl_mem), (void *) &buffer_mem);
            err = clEnqueueNDRangeKernel(ufo_buffer_get_command_queue(buffer),
                                         self->priv->kernel,
                                         1, NULL, global_work_size, NULL,
                                         0, NULL, &event);
            ufo_buffer_wait_on_event(buffer, event);
            */
        }
        ufo_resource_manager_release_buffer(manager, sinogram);
        sinogram = (UfoBuffer *) g_async_queue_pop(input_queue);
    }

    g_async_queue_push(output_queue, 
            ufo_resource_manager_request_finish_buffer(manager));
}

static void ufo_filter_backproject_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterBackproject *self = UFO_FILTER_BACKPROJECT(object);
    switch (property_id) {
        case PROP_NUM_SINOGRAMS:
            self->priv->num_sinograms = g_value_get_int(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_backproject_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterBackproject *self = UFO_FILTER_BACKPROJECT(object);
    switch (property_id) {
        case PROP_NUM_SINOGRAMS:
            g_value_set_int(value, self->priv->num_sinograms);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_backproject_class_init(UfoFilterBackprojectClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    EthosPluginClass *plugin_class = ETHOS_PLUGIN_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_backproject_set_property;
    gobject_class->get_property = ufo_filter_backproject_get_property;
    plugin_class->activated = activated;
    plugin_class->deactivated = deactivated;
    filter_class->initialize = ufo_filter_backproject_initialize;
    filter_class->process = ufo_filter_backproject_process;

    /* install properties */
    backproject_properties[PROP_NUM_SINOGRAMS] = 
        g_param_spec_int("num-sinograms",
            "Number of sinograms",
            "Number of to process",
            -1,   /* minimum */
            8192,   /* maximum */
            1,   /* default */
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_NUM_SINOGRAMS, backproject_properties[PROP_NUM_SINOGRAMS]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterBackprojectPrivate));
}

static void ufo_filter_backproject_init(UfoFilterBackproject *self)
{
    self->priv = UFO_FILTER_BACKPROJECT_GET_PRIVATE(self);
}

G_MODULE_EXPORT EthosPlugin *ethos_plugin_register(void)
{
    return g_object_new(UFO_TYPE_FILTER_BACKPROJECT, NULL);
}
