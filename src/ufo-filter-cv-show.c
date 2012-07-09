#include <gmodule.h>
#include <cv.h>
#include <highgui.h>
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <ufo/ufo-resource-manager.h>
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
    gboolean    show_histogram;
    gchar      *window_name;
    IplImage   *image;
    IplImage   *blit;
};

G_DEFINE_TYPE(UfoFilterCvShow, ufo_filter_cv_show, UFO_TYPE_FILTER_SINK)

#define UFO_FILTER_CV_SHOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_FILTER_CV_SHOW, UfoFilterCvShowPrivate))

enum {
    PROP_0,
    PROP_SHOW_HISTOGRAM,
    N_PROPERTIES
};

static GParamSpec *cv_show_properties[N_PROPERTIES] = { NULL, };

static void
ufo_filter_cv_show_initialize(UfoFilterSink *filter, UfoBuffer *params[], GError **error)
{
    UfoFilterCvShowPrivate *priv = UFO_FILTER_CV_SHOW_GET_PRIVATE (filter);
    CvSize size;

    priv->window_name = g_strdup_printf ("Foo-%p", (gpointer) filter);
    cvNamedWindow (priv->window_name, CV_WINDOW_AUTOSIZE);
    cvMoveWindow (priv->window_name, 100, 100);

    priv->image = cvCreateImageHeader (size, IPL_DEPTH_32F, 1);
    priv->blit = cvCreateImage (size, IPL_DEPTH_8U, 1);
}

static void
ufo_filter_cv_show_process_cpu (UfoFilterSink *filter, UfoBuffer *params[], gpointer cmd_queue, GError **error)
{
    UfoFilterCvShowPrivate *priv = UFO_FILTER_CV_SHOW_GET_PRIVATE (filter);
    CvSize size;

    ufo_buffer_get_2d_dimensions (params[0], (guint *) &size.width, (guint *) &size.height);

    priv->image->imageData = (char *) ufo_buffer_get_host_array (params[0], (cl_command_queue) cmd_queue);

    cvConvertImage (priv->image, priv->blit, 0);
    cvShowImage (priv->window_name, priv->image);
    cvWaitKey (30);
}

static void
ufo_filter_cv_show_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    UfoFilterCvShow *self = UFO_FILTER_CV_SHOW(object);

    switch (property_id) {
        case PROP_SHOW_HISTOGRAM:
            self->priv->show_histogram = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_cv_show_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    UfoFilterCvShow *self = UFO_FILTER_CV_SHOW(object);

    switch (property_id) {
        case PROP_SHOW_HISTOGRAM:
            g_value_set_boolean(value, self->priv->show_histogram);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void
ufo_filter_cv_show_finalize (GObject *object)
{
    UfoFilterCvShowPrivate *priv = UFO_FILTER_CV_SHOW_GET_PRIVATE (object);

    cvReleaseImage (&priv->image);
    cvReleaseImage (&priv->blit);
    cvDestroyWindow (priv->window_name);
    g_free (priv->window_name);

    G_OBJECT_CLASS(ufo_filter_cv_show_parent_class)->finalize(object);
}

static void
ufo_filter_cv_show_class_init (UfoFilterCvShowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    UfoFilterSinkClass *filter_class = UFO_FILTER_SINK_CLASS (klass);

    gobject_class->set_property = ufo_filter_cv_show_set_property;
    gobject_class->get_property = ufo_filter_cv_show_get_property;
    gobject_class->finalize = ufo_filter_cv_show_finalize;
    filter_class->initialize = ufo_filter_cv_show_initialize;
    filter_class->consume = ufo_filter_cv_show_process_cpu;

    cv_show_properties[PROP_SHOW_HISTOGRAM] =
        g_param_spec_boolean("show-histogram",
            "Show also the histogram of the buffer",
            "Show also the histogram of the buffer",
            FALSE,
            G_PARAM_READWRITE);

    g_object_class_install_property(gobject_class, PROP_SHOW_HISTOGRAM, cv_show_properties[PROP_SHOW_HISTOGRAM]);

    g_type_class_add_private(gobject_class, sizeof(UfoFilterCvShowPrivate));
}

static void
ufo_filter_cv_show_init(UfoFilterCvShow *self)
{
    UfoFilterCvShowPrivate *priv = self->priv = UFO_FILTER_CV_SHOW_GET_PRIVATE(self);
    priv->show_histogram = FALSE;

    ufo_filter_register_inputs (UFO_FILTER(self), 2, NULL);
}

G_MODULE_EXPORT UfoFilter *
ufo_filter_plugin_new(void)
{
    return g_object_new(UFO_TYPE_FILTER_CV_SHOW, NULL);
}
