#ifndef UFO_PRIV_H
#define UFO_PRIV_H

#include <glib.h>

G_BEGIN_DECLS

#define g_list_for(list, it) \
        for (it = g_list_first (list); \
             it != NULL; \
             it = g_list_next (it))

guint ceil_power_of_two (guint x);

G_END_DECLS

#endif
