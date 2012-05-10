#include <gmodule.h>
#include <cv.h>
#include <highgui.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
#include <ufo/ufo-filter.h>
#include <ufo/ufo-buffer.h>

#include "ufo-filter-cv-show.h"

/**
 * SECTION:ufo-filter-cv-show
 * @Short_description: Show input using OpenCV
 * @Title: cvshow
 *
 * Display any data using the OpenCV library. An optional histogram can be shown
 * if #UfoFilterCvShow:show-histogram is enabled.
 */

struct _UfoFilterCvShowPrivate {
    gboolean show_histogram;
};

G_DEFINE_TYPE(UfoFilterCvShow, ufo_filter_cv_show, UFO_TYPE_FILTER)

#define UFO_FILTER_CV_SHOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_CV_SHOW, UfoFilterCvShowPrivate))

enum {
    PROP_0,
    PROP_SHOW_HISTOGRAM, 
    N_PROPERTIES
};

static GParamSpec *cv_show_properties[N_PROPERTIES] = { NULL, };

static void ufo_filter_cv_show_initialize(UfoFilter *filter, UfoBuffer *params)
{
}

static GError *ufo_filter_cv_show_process(UfoFilter *filter)
{
    UfoChannel *input_channel = ufo_filter_get_input_channel(filter);
    cl_command_queue command_queue = (cl_command_queue) ufo_filter_get_command_queue(filter);

    CvSize size;
    UfoBuffer *input = ufo_channel_get_input_buffer(input_channel);

    if (input == NULL)
        return;

    ufo_buffer_get_2d_dimensions(input, (guint *) &size.width, (guint *) &size.height);

    IplImage *image = cvCreateImageHeader(size, IPL_DEPTH_32F, 1);
    IplImage *blit = cvCreateImage(size, IPL_DEPTH_8U, 1);

    gchar *window_name = g_strdup_printf("Foo-%p", filter);
    cvNamedWindow(window_name, CV_WINDOW_AUTOSIZE);
    cvMoveWindow(window_name, 100, 100);

    while (input != NULL) {
        image->imageData = (char *) ufo_buffer_get_host_array(input, command_queue);

        cvConvertImage(image, blit, 0);
        cvShowImage(window_name, image);
        cvWaitKey(30);
        
        ufo_channel_finalize_input_buffer(input_channel, input);
        input = ufo_channel_get_input_buffer(input_channel);
    }

    cvWaitKey(10000);
    cvDestroyWindow(window_name);
    g_free(window_name);
    return NULL;
}

static void ufo_filter_cv_show_set_property(GObject *object,
    guint           property_id,
    const GValue    *value,
    GParamSpec      *pspec)
{
    UfoFilterCvShow *self = UFO_FILTER_CV_SHOW(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_SHOW_HISTOGRAM:
            self->priv->show_histogram = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_cv_show_get_property(GObject *object,
    guint       property_id,
    GValue      *value,
    GParamSpec  *pspec)
{
    UfoFilterCvShow *self = UFO_FILTER_CV_SHOW(object);

    /* Handle all properties accordingly */
    switch (property_id) {
        case PROP_SHOW_HISTOGRAM:
            g_value_set_boolean(value, self->priv->show_histogram);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void ufo_filter_cv_show_class_init(UfoFilterCvShowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    UfoFilterClass *filter_class = UFO_FILTER_CLASS(klass);

    gobject_class->set_property = ufo_filter_cv_show_set_property;
    gobject_class->get_property = ufo_filter_cv_show_get_property;
    filter_class->initialize = ufo_filter_cv_show_initialize;
    filter_class->process = ufo_filter_cv_show_process;

    /* install properties */
    cv_show_properties[PROP_SHOW_HISTOGRAM] = 
        g_param_spec_boolean("show-histogram",
            "Show also the histogram of the buffer",
            "Show also the histogram of the buffer",
            FALSE,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_SHOW_HISTOGRAM, cv_show_properties[PROP_SHOW_HISTOGRAM]);

    /* install private data */
    g_type_class_add_private(gobject_class, sizeof(UfoFilterCvShowPrivate));
}

static void ufo_filter_cv_show_init(UfoFilterCvShow *self)
{
    UfoFilterCvShowPrivate *priv = self->priv = UFO_FILTER_CV_SHOW_GET_PRIVATE(self);
    priv->show_histogram = FALSE;

    ufo_filter_register_input(UFO_FILTER(self), "input0", 2);
}

G_MODULE_EXPORT UfoFilter *ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_CV_SHOW, NULL);
}
