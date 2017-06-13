#ifndef __ROFEX_H
#define __ROFEX_H

#include <ufo/ufo.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

G_BEGIN_DECLS

void
copy_gvarray_gint (GValueArray *gv_array,
                   gint        **array,
                   guint       *n_items);

void
copy_gvarray_guint (GValueArray *gv_array,
                    guint       **array,
                    guint       *n_items);

cl_mem
copy_gvarray_guint_to_gpu (GValueArray *gv_array,
                           gpointer    context,
                           gpointer    cmd_queue,
                           GError      **error);

cl_mem
copy_gvarray_gint_to_gpu (GValueArray *gv_array,
                          gpointer    context,
                          gpointer    cmd_queue,
                          GError      **error);

cl_mem
read_file_to_gpu (const gchar *filepath,
                  gpointer    context,
                  gpointer    cmd_queue,
                  GError      **error);

void
set_default_rings_selection_mask (GValueArray **array);

void
set_default_beam_positions (GValueArray **array);

G_END_DECLS

#endif
