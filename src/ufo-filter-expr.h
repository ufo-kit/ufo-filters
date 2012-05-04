#ifndef __UFO_FILTER_EXPR_H
#define __UFO_FILTER_EXPR_H

#include <glib.h>
#include <glib-object.h>

#include <ufo/ufo-filter.h>

#define UFO_TYPE_FILTER_EXPR             (ufo_filter_expr_get_type())
#define UFO_FILTER_EXPR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_FILTER_EXPR, UfoFilterExpr))
#define UFO_IS_FILTER_EXPR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_FILTER_EXPR))
#define UFO_FILTER_EXPR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), UFO_TYPE_FILTER_EXPR, UfoFilterExprClass))
#define UFO_IS_FILTER_EXPR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), UFO_TYPE_FILTER_EXPR))
#define UFO_FILTER_EXPR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), UFO_TYPE_FILTER_EXPR, UfoFilterExprClass))

typedef struct _UfoFilterExpr           UfoFilterExpr;
typedef struct _UfoFilterExprClass      UfoFilterExprClass;
typedef struct _UfoFilterExprPrivate    UfoFilterExprPrivate;

struct _UfoFilterExpr {
    /*< private >*/
    UfoFilter parent_instance;

    UfoFilterExprPrivate *priv;
};

/**
 * UfoFilterExprClass:
 *
 * #UfoFilterExpr class
 */
struct _UfoFilterExprClass {
    /*< private >*/
    UfoFilterClass parent_class;
};

GType ufo_filter_expr_get_type(void);
UfoFilter *ufo_filter_plugin_new(void);

#endif
