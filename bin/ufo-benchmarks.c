#include <glib.h>
#include <ufo/ufo.h>

static void
measure_fbp (GError **error)
{
    UfoPluginManager *pm;
    UfoTaskGraph *graph;
    UfoBaseScheduler *sched;
    UfoTaskNode *stream;
    UfoTaskNode *null;
    UfoTaskNode *fft;
    UfoTaskNode *filter;
    UfoTaskNode *ifft;
    UfoTaskNode *backproject;
    gdouble time;

    pm = ufo_plugin_manager_new ();

    stream = ufo_plugin_manager_get_task (pm, "dummy-data", error);
    null = ufo_plugin_manager_get_task (pm, "null", error);
    fft = ufo_plugin_manager_get_task (pm, "fft", error);
    ifft = ufo_plugin_manager_get_task (pm, "ifft", error);
    filter = ufo_plugin_manager_get_task (pm, "filter", error);
    backproject = ufo_plugin_manager_get_task (pm, "backproject", error);

    g_object_set (stream, "width", 4096, "height", 4096, "number", 4096, NULL);

    graph = UFO_TASK_GRAPH (ufo_task_graph_new ());

    ufo_task_graph_connect_nodes (graph, stream, fft);
    ufo_task_graph_connect_nodes (graph, fft, filter);
    ufo_task_graph_connect_nodes (graph, filter, ifft);
    ufo_task_graph_connect_nodes (graph, ifft, backproject);
    ufo_task_graph_connect_nodes (graph, backproject, null);

    sched = ufo_scheduler_new ();

    ufo_base_scheduler_run (sched, graph, error);
    g_object_get (sched, "time", &time, NULL);

    g_object_unref (sched);
    g_object_unref (stream);
    g_object_unref (null);
    g_object_unref (fft);
    g_object_unref (ifft);
    g_object_unref (filter);
    g_object_unref (backproject);
    g_object_unref (pm);
}

int 
main (int argc, char const* argv[])
{
    GError *error;
    
    error = NULL;

    measure_fbp (&error);

    if (error != NULL)
        g_print ("Error: %s\n", error->message);

    return 0;
}
