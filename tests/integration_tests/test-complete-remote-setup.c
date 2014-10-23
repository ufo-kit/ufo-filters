/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ufo/ufo.h>
#include <glib/gstdio.h>
#include "test-suite.h"

typedef struct {
    UfoDaemon *daemon;
    gchar *tmpdir;
} Fixture;

static void
setup (Fixture *fixture, gconstpointer data)
{
    gchar *addr = g_strdup ("tcp://127.0.0.1:5555");

    fixture->daemon = ufo_daemon_new (addr);
    ufo_daemon_start (fixture->daemon);

    fixture->tmpdir = g_strdup ("ufotemp-XXXXXX");
    g_mkdtemp (fixture->tmpdir);
}

static void
teardown (Fixture *fixture, gconstpointer data)
{
    ufo_daemon_stop (fixture->daemon);
    g_object_unref (fixture->daemon);

    g_rmdir (fixture->tmpdir);
    g_free (fixture->tmpdir);
}

static void
test_simple_invert (Fixture *fixture,
                            gconstpointer unused)
{
    //double-invert an image should equal original image
    UfoPluginManager *mgr = ufo_plugin_manager_new ();

    const gchar *input_image = "../data/sinogram-00000.tif";
    const gchar *output_image = g_strconcat (fixture->tmpdir, "/", "sinogram-00000-inverted.tif", NULL);

    const gchar *kernel_source = 
    "__kernel void invert(__global float *input, __global float *output)       "
    "{                                                                         "
    "int index = get_global_id(1) * get_global_size(0) + get_global_id(0);     "
    "output[index] = 1.0f - input[index];                                      "
    "}                                                                         "
    ;

    UfoTaskNode *reader = ufo_plugin_manager_get_task (mgr, "reader", NULL);
    UfoTaskNode *writer = ufo_plugin_manager_get_task (mgr, "writer", NULL);
    UfoTaskNode *cl1 = ufo_plugin_manager_get_task (mgr, "opencl", NULL);
    UfoTaskNode *cl2 = ufo_plugin_manager_get_task (mgr, "opencl", NULL);

    GValue input_file = G_VALUE_INIT,
           output_file = G_VALUE_INIT,
           output_single = G_VALUE_INIT,
           kernel_code = G_VALUE_INIT,
           kernel_name = G_VALUE_INIT;

    g_value_init (&input_file, G_TYPE_STRING);
    g_value_init (&output_file, G_TYPE_STRING);
    g_value_init (&output_single, G_TYPE_BOOLEAN);
    g_value_init (&kernel_code, G_TYPE_STRING);
    g_value_init (&kernel_name, G_TYPE_STRING);

    g_value_set_static_string (&input_file, input_image);
    g_object_set_property (G_OBJECT (reader), "path", &input_file);

    g_value_set_static_string (&output_file, output_image);
    g_object_set_property (G_OBJECT (writer), "filename", &output_file);
    g_value_set_boolean (&output_single, TRUE);
    g_object_set_property (G_OBJECT (writer), "single-file", &output_single);

    g_value_set_static_string (&kernel_code, kernel_source);
    g_object_set_property (G_OBJECT (cl1), "source", &kernel_code);
    g_object_set_property (G_OBJECT (cl2), "source", &kernel_code);
    g_value_set_static_string (&kernel_name, "invert");
    g_object_set_property (G_OBJECT (cl1), "kernel", &kernel_name);
    g_object_set_property (G_OBJECT (cl2), "kernel", &kernel_name);

    UfoGraph *graph = UFO_GRAPH (ufo_task_graph_new ());
    ufo_graph_connect_nodes (graph, UFO_NODE (reader), UFO_NODE (cl1), NULL);
    ufo_graph_connect_nodes (graph, UFO_NODE (cl1), UFO_NODE (cl2), NULL);
    ufo_graph_connect_nodes (graph, UFO_NODE (cl2), UFO_NODE (writer), NULL);

    gchar *remote = g_strdup ("tcp://127.0.0.1:5555");
    GList *remotes = g_list_append (NULL, remote);
    UfoBaseScheduler *sched = ufo_scheduler_new ();

    ufo_base_scheduler_run (sched, UFO_TASK_GRAPH (graph), NULL);
    g_free(remote);

    // test that an output was generated
    g_assert (g_file_test (output_image, G_FILE_TEST_EXISTS));

    // check that the file size matches our expectation
    GMappedFile *file_out = g_mapped_file_new (output_image, FALSE, NULL);
    gsize len_actual = g_mapped_file_get_length (file_out);
    gsize len_expected = 1048722;

    g_assert (len_expected == len_actual);

    g_mapped_file_unref (file_out);
    g_remove (output_image);

    g_object_unref (reader);
    g_object_unref (writer);
    g_object_unref (cl1);
    g_object_unref (cl2);
    g_object_unref (graph);
    g_object_unref (mgr);
}

void
test_add_complete_remote_setup (void)
{
    g_test_add ("/complete_remote_setup/simple_invert",
                Fixture, NULL,
                setup, test_simple_invert, teardown);
}
