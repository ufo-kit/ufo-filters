#include <stdio.h>
#include "rofex.h"

void
copy_gvarray_gint(GValueArray *gv_array,
                  gint       **array,
                  guint      *n_items)
{
    GValue *gval;
    gint val;

    //
    *n_items = gv_array->n_values;
    *array = g_malloc(*n_items * sizeof(gint));

    // Copy elements one by one
    for (guint i = 0; i < gv_array->n_values; i++) {
        gval = g_value_array_get_nth (gv_array, i);
        if (gval) {
            val =  g_value_get_int(gval);
            (*array)[i] = val;
        } else {
            g_error("The value at index %d has an unexpected type.", i);

            g_free(*array);
            *array = NULL;
            *n_items = 0;
            return;
        }
    }
}

void
copy_gvarray_guint(GValueArray *gv_array,
                   guint       **array,
                   guint       *n_items)
{
    GValue *gval;
    guint val;

    //
    *n_items = gv_array->n_values;
    *array = g_malloc(*n_items * sizeof(guint));

    // Copy elements one by one
    for (guint i = 0; i < gv_array->n_values; i++) {
        gval = g_value_array_get_nth (gv_array, i);
        if (gval) {
            val =  g_value_get_uint(gval);
            (*array)[i] = val;
        } else {
            g_error("The value at index %d has an unexpected type.", i);

            g_free(*array);
            *array = NULL;
            *n_items = 0;
            return;
        }
    }
}

cl_mem
copy_gvarray_guint_to_gpu (GValueArray *gv_array,
                           gpointer    context,
                           gpointer    cmd_queue,
                           GError      **error)
{
    guint *buffer;
    cl_mem d_buffer;
    guint n_items, n_bytes;
    cl_int err;

    copy_gvarray_guint(gv_array, &buffer, &n_items);
    n_bytes = n_items * sizeof(guint);

    if (buffer == NULL) {
        return NULL;
    }

    // Allocate GPU memory.
    d_buffer = clCreateBuffer (context,
                               CL_MEM_READ_WRITE,
                               n_bytes,
                               NULL,
                               &err);

    UFO_RESOURCES_CHECK_CLERR (err);

    // Copy data to GPU
    err = clEnqueueWriteBuffer (cmd_queue,
                                d_buffer,
                                CL_TRUE,
                                0, n_bytes,
                                buffer,
                                0,
                                NULL,
                                NULL);

    UFO_RESOURCES_CHECK_CLERR (err);

    // Free temporary buffer
    g_free (buffer);

    return d_buffer;
}

cl_mem
copy_gvarray_gint_to_gpu (GValueArray *gv_array,
                          gpointer    context,
                          gpointer    cmd_queue,
                          GError      **error)
{
    gint *buffer;
    cl_mem d_buffer;
    guint n_items, n_bytes;
    cl_int err;

    copy_gvarray_gint(gv_array, &buffer, &n_items);
    n_bytes = n_items * sizeof(gint);

    if (buffer == NULL) {
        return NULL;
    }

    // Allocate GPU memory.
    d_buffer = clCreateBuffer (context,
                               CL_MEM_READ_WRITE,
                               n_bytes,
                               NULL,
                               &err);

    UFO_RESOURCES_CHECK_CLERR (err);

    // Copy data to GPU
    err = clEnqueueWriteBuffer (cmd_queue,
                                d_buffer,
                                CL_TRUE,
                                0, n_bytes,
                                buffer,
                                0,
                                NULL,
                                NULL);

    UFO_RESOURCES_CHECK_CLERR (err);

    // Free temporary buffer
    g_free (buffer);

    return d_buffer;
}

cl_mem
read_file_to_gpu (const gchar *filepath,
                  gpointer    context,
                  gpointer    cmd_queue,
                  GError      **error)
{
    FILE  * pFile;
    gpointer buffer;
    cl_mem d_buffer;
    gsize n_bytes, bytes_read;
    cl_int err;

    // Try to open file
    pFile = fopen (filepath , "rb");
    if (pFile == NULL) {
        g_error("File %s cannot be read.", filepath);
        return NULL;
    }

    // Determine the file size.
    fseek (pFile , 0 , SEEK_END);
    n_bytes = ftell (pFile);
    rewind (pFile);

    // Allocate memory for temporary buffer
    buffer = g_malloc (n_bytes);
    if (buffer == NULL) {
        fclose (pFile);
        g_error ("Memory cannot be allocated (%ld bytes).", n_bytes);
    }

    // Read data from the file to temporary buffer
    bytes_read = fread (buffer, 1, n_bytes, pFile);
    if (bytes_read != n_bytes) {
        fclose (pFile);
        g_free(buffer);
        g_error ("The wrong number of bytes has been read.");
    }

    // Allocate GPU memory.
    d_buffer = clCreateBuffer (context,
                               CL_MEM_READ_WRITE,
                               n_bytes,
                               NULL,
                               &err);

    UFO_RESOURCES_CHECK_CLERR (err);

    // Copy data to GPU
    err = clEnqueueWriteBuffer (cmd_queue,
                                d_buffer,
                                CL_TRUE,
                                0, n_bytes,
                                buffer,
                                0,
                                NULL,
                                NULL);

    UFO_RESOURCES_CHECK_CLERR (err);

    // Free temporary buffer
    g_free (buffer);

    return d_buffer;
}


void
set_default_rings_selection_mask (GValueArray **array)
{
    GValue gval = {0};
    g_value_init(&gval, G_TYPE_INT);

    /*
    // Process only the ring on which the beam is directed.
    *array = g_value_array_new (1);
    g_value_set_int (&gval, 0);
    g_value_array_insert (*array, 0, &gval);
    */

    // Process the ring on which the beam is directed and the adjacent rings.
    *array = g_value_array_new (3);
    g_value_set_int (&gval, -1);
    g_value_array_insert (*array, 0, &gval);
    g_value_set_int (&gval, 0);
    g_value_array_insert (*array, 1, &gval);
    g_value_set_int (&gval, 1);
    g_value_array_insert (*array, 2, &gval);
}

void
set_default_beam_positions (GValueArray **array)
{
    GValue gval = {0};
    g_value_init(&gval, G_TYPE_UINT);

    *array = g_value_array_new (2);
    g_value_set_uint (&gval, 1);
    g_value_array_insert (*array, 0, &gval);

    g_value_set_uint (&gval, 0);
    g_value_array_insert (*array, 1, &gval);
}
