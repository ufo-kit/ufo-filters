/*
 * Gathering statistics on a image stream, copying input to output
 * This file is part of ufo-serge filter set.
 * Copyright (C) 2016 Serge Cohen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Serge Cohen <serge.cohen@synchrotron-soleil.fr>
 */


#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ufo-stat-monitor-task.h"
#include "ufo-sxc-common.h"

struct _UfoStatMonitorTaskPrivate {
    FILE * stat_file;
    gchar * stat_fn;
    gboolean trace_count;
    gboolean be_quiet;
    gboolean node_has_fp64;
    cl_kernel kernel;
    cl_kernel kernel_final;
    gsize im_index;
    guint n_items;
    cl_ulong max_local_mem;
    size_t local_scratch_size;
    cl_uint wg_size;
    cl_uint wg_num;
    cl_mem stat_out_buff;   /* The buffer used by the kernel to output its results */
    cl_mem stat_out_red;    /* The buffer used by the final reduction kernel */
    guint sm_index;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoStatMonitorTask, ufo_stat_monitor_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_STAT_MONITOR_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_STAT_MONITOR_TASK, UfoStatMonitorTaskPrivate))

enum {
  PROP_0,
  PROP_NUM_ITEMS,
  PROP_STAT_FN,
  PROP_TRACE,
  PROP_QUIET,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_stat_monitor_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_STAT_MONITOR_TASK, NULL));
}

static void
ufo_stat_monitor_task_setup (UfoTask *task,
                             UfoResources *resources,
                             GError **error)
{
    UfoStatMonitorTaskPrivate *priv;
    UfoGpuNode *node;
    cl_command_queue cmd_queue;
    cl_context context_cl;
    cl_device_id dev_cl;
    cl_int err_code;
    cl_uint num_cu;
    size_t max_wgs, max_wis[3];
    size_t ker_pref_wgs;

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    UFO_RESOURCES_CHECK_CLERR (clGetCommandQueueInfo (cmd_queue, CL_QUEUE_DEVICE, sizeof (cl_device_id), &dev_cl, NULL));
    UFO_RESOURCES_CHECK_CLERR (clGetCommandQueueInfo (cmd_queue, CL_QUEUE_CONTEXT, sizeof (cl_context), &context_cl, NULL));

    priv = UFO_STAT_MONITOR_TASK_GET_PRIVATE (task);

    priv->node_has_fp64 = device_has_extension (node, "cl_khr_fp64");

    /* Error : kernel compilation is buggy is one device has fp64 and another has not ! */
    if (priv->node_has_fp64) {
        priv->kernel = ufo_resources_get_kernel (resources, "stat-monitor.cl", "stat_monitor_f64", NULL, error);
        priv->kernel_final = ufo_resources_get_kernel (resources, "stat-monitor.cl", "stat_monitor_f64_fin", NULL, error);
    }
    else {
        priv->kernel = ufo_resources_get_kernel (resources, "stat-monitor.cl", "stat_monitor_f32", NULL, error);
        priv->kernel_final = ufo_resources_get_kernel (resources, "stat-monitor.cl", "stat_monitor_f32_fin", NULL, error);
    }

    if (priv->kernel == NULL || priv->kernel_final == NULL)
        return;

    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel));
    UFO_RESOURCES_CHECK_CLERR (clRetainKernel (priv->kernel_final));

    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(cl_ulong), &(priv->max_local_mem), NULL));
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &num_cu, NULL));
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &max_wgs, NULL));
    UFO_RESOURCES_CHECK_CLERR (clGetDeviceInfo (dev_cl, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(max_wis), max_wis, NULL));
    UFO_RESOURCES_CHECK_CLERR (clGetKernelWorkGroupInfo (priv->kernel, dev_cl, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &ker_pref_wgs, NULL));

    /* 4 work-groups per comput unit. */
    priv->wg_num = num_cu << 2;

    if (priv->wg_num > max_wis[0]) { 
        /*
         * We have to reduce the number of workgroup so that last reduction can
         * be done in a single workgroup (hence 1-value per workroup should
         * eventually fit within a single workgroup.
         */
        priv->wg_num = max_wis[0];
    }

    priv->wg_size = ker_pref_wgs;

    if (priv->wg_size > max_wis[0]) {
        priv->wg_size = max_wis[0];
    }

    if (priv->wg_size < priv->wg_num) { // To ensure the final reduction step.
        priv->wg_num = priv->wg_size;
    }

    /* dividing by 2 the total local memory. */
    priv->local_scratch_size = (size_t) (priv->max_local_mem >> 1);

    if (priv->node_has_fp64) {
        /*
         * Each workgroup needs 4 items * 8 bytes per work-item, that is
         * (wg_size << 5) B of local memory.
         */
        if (priv->local_scratch_size > (priv->wg_size << 5))
            priv->local_scratch_size = (priv->wg_size << 5);
        else
            priv->wg_size = (priv->local_scratch_size) >> 5;
    }
    else {
        /*
         * Each workgroup needs 4 items * 4 bytes per work-item, that is
         * (wg_size << 4) B of local memory.
         */
        if (priv->local_scratch_size > (priv->wg_size << 4))
            priv->local_scratch_size = (priv->wg_size << 4);
        else
            priv->wg_size = (priv->local_scratch_size) >> 4;
    }

    priv->im_index = 0;

    /* Opening (if required) the statistic file */
    if (strcmp ("-", priv->stat_fn)) {
        priv->stat_file = fopen (priv->stat_fn, "a");
        fprintf (stdout, "stat-monitor (%u) will outputs its results to file '%s'\n", priv->sm_index, priv->stat_fn);
        fprintf (priv->stat_file, "# index min max sum sum-of-sq mean var\n");
    }
    else {
        priv->stat_file = stdout;
        fprintf (stdout, "stat-monitor (%u) will outputs its results to stdout\n", priv->sm_index);
    }

    /* Allocating once for all the output buffer that will be used for statistcis output. */
    if ( priv->node_has_fp64 ) {
        /* min, max, mean, sd (one 4-tuple per work-group) */
        priv->stat_out_buff = clCreateBuffer (context_cl, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS, priv->wg_num << 5, NULL, &err_code);
        UFO_RESOURCES_CHECK_CLERR (err_code);
        /* min, max, mean, sd (one 4-tuple once only) */
        priv->stat_out_red = clCreateBuffer (context_cl, CL_MEM_READ_WRITE | CL_MEM_HOST_READ_ONLY, 1 << 5, NULL, &err_code);
        UFO_RESOURCES_CHECK_CLERR (err_code);
    }
    else {
        /* min, max, mean, sd (one 4-tuple per work-group) */
        priv->stat_out_buff = clCreateBuffer (context_cl, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS, priv->wg_num << 4, NULL, &err_code);
        UFO_RESOURCES_CHECK_CLERR (err_code);
        /* min, max, mean, sd (one 4-tuple once only) */
        priv->stat_out_red = clCreateBuffer (context_cl, CL_MEM_READ_WRITE | CL_MEM_HOST_READ_ONLY, 1 << 4, NULL, &err_code);
        UFO_RESOURCES_CHECK_CLERR (err_code);
    }
}

static void
ufo_stat_monitor_task_get_requisition (UfoTask *task,
                                       UfoBuffer **inputs,
                                       UfoRequisition *requisition,
                                       GError **error)
{
    /* In the current version the statistics are NEVER the output of the filter.
     * Indeed is behaving as a /pass-through/ filter, doing nothing to the
     * image. */
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static guint
ufo_stat_monitor_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_stat_monitor_task_get_num_dimensions (UfoTask *task,
                                          guint input)
{
    return 2;
}

static UfoTaskMode
ufo_stat_monitor_task_get_mode (UfoTask *task)
{
    /* We are still needing the GPU (OpenCL device indeed) to perform the
     * statistics computation. */
    return UFO_TASK_MODE_PROCESSOR | UFO_TASK_MODE_GPU;
}

/* Copied over from the monitor task, as we are trying to mimick it (plus
 * statistics gathering/printing) */
static gchar *
join_list (GList *list, const gchar *sep)
{
    gchar **array;
    GList *it;
    gchar *result;
    guint i = 0;

    array = g_new0 (gchar *, g_list_length (list) + 1);

    g_list_for (list, it)
        array[i++] = it->data;

    result = g_strjoinv (sep, array);
    g_free (array);
    return result;
}

static gboolean
ufo_stat_monitor_task_process (UfoTask *task,
                               UfoBuffer **inputs,
                               UfoBuffer *output,
                               UfoRequisition *requisition)
{
    UfoStatMonitorTaskPrivate *priv;
    priv = UFO_STAT_MONITOR_TASK_GET_PRIVATE (task);

    UfoGpuNode *node;
    UfoProfiler *profiler;
    cl_command_queue cmd_queue;
    cl_mem in_mem;
    cl_uint img_size;
    UfoRequisition img_req;

    UfoBufferLocation location;
    GList *keys;
    GList *sizes;
    gchar *keystring;
    gchar *dimstring;

    /* Getting information from the buffer, before computing statistics */
    location = ufo_buffer_get_location (inputs[0]);
    keys = ufo_buffer_get_metadata_keys (inputs[0]);
    sizes = NULL;

    /* Launching the kernel first, so that it has a bit of extra time while CPU is running */
    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);

    in_mem = ufo_buffer_get_device_array (inputs[0], cmd_queue);
    ufo_buffer_get_requisition (inputs[0], &img_req);

    img_size = 1;

    for (guint i = 0; i < img_req.n_dims; i++)
        img_size *= img_req.dims[i];

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 0, sizeof (cl_mem), &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 1, sizeof (cl_mem), &(priv->stat_out_buff)));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 2, sizeof (cl_uint), &img_size));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel, 3, priv->local_scratch_size, NULL));

    gsize total_wi = priv->wg_num * priv->wg_size;
    gsize wg_size = (gsize)(priv->wg_size);

    gsize true_wi = (total_wi < (gsize)img_size) ? total_wi : (gsize)img_size;
    gsize true_wg_num = ((true_wi-1) / priv->wg_size) + 1;

    /* Making sure that true_wi is a mulitple of work group size. */
    true_wi = priv->wg_size * true_wg_num;

    profiler = ufo_task_node_get_profiler (UFO_TASK_NODE (task));

    /* First reduction step : */
    ufo_profiler_call (profiler, cmd_queue, priv->kernel, 1, &true_wi, &wg_size);

    /* At this time, we need to have a second kernel to further reduce the
     * results of the previous results that where produced at the rate of one
     * per work-group. */

    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel_final, 0, sizeof (cl_mem), &(priv->stat_out_buff)));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel_final, 1, sizeof (cl_mem), &(priv->stat_out_red)));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel_final, 2, sizeof (cl_uint), &(true_wg_num)));

    if (priv->node_has_fp64) {
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel_final, 3, sizeof(cl_double)*4*true_wg_num, NULL));
    }
    else {
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (priv->kernel_final, 3, sizeof(cl_float)*4*true_wg_num, NULL));
    }

    total_wi = (true_wg_num & 0x1) + (true_wg_num >> 1);

    /* Second reduction step : */
    ufo_profiler_call(profiler, cmd_queue, priv->kernel_final, 1, &total_wi, &total_wi);

    if (priv->node_has_fp64) {
        double stat_res[6];
        double img_size_f = (double) img_size;

        UFO_RESOURCES_CHECK_CLERR (clEnqueueReadBuffer (cmd_queue, priv->stat_out_red, CL_TRUE, 0, 4<<3, stat_res, 0, NULL, NULL));

        stat_res[4] = stat_res[2] / img_size_f;
        stat_res[5] = (stat_res[3] - img_size_f * stat_res[4] * stat_res[4]) / (img_size_f - 1.0);

        if (stdout == priv->stat_file) {
            fprintf (priv->stat_file, "(%u) ", priv->sm_index);
        }
        fprintf (priv->stat_file, "%zu %le %le %le %le %le %le\n", priv->im_index, stat_res[0], stat_res[1], stat_res[2], stat_res[3], stat_res[4], stat_res[5]);
    }
    else {
        float stat_res[6];
        float img_size_f = (float)img_size;

        UFO_RESOURCES_CHECK_CLERR (clEnqueueReadBuffer(cmd_queue, priv->stat_out_red, CL_TRUE, 0, 4<<2, stat_res, 0, NULL, NULL));

        stat_res[4] = stat_res[2] / img_size_f;
        stat_res[5] = (stat_res[3] - img_size_f * stat_res[4] * stat_res[4]) / (img_size_f - 1.0f);

        if (stdout == priv->stat_file) {
            fprintf (priv->stat_file, "(%u) ", priv->sm_index);
        }
        fprintf (priv->stat_file, "%zu %e %e %e %e %e %e\n", priv->im_index, stat_res[0], stat_res[1], stat_res[2], stat_res[3], stat_res[4], stat_res[5]);
    }

    if (priv->trace_count)
        fprintf (stdout, "stat-monitor (%u) : done frame %zu\n", priv->sm_index, priv->im_index);

    ++(priv->im_index);

    if (!priv->be_quiet) {
        for (guint i = 0; i < requisition->n_dims; i++)
            sizes = g_list_append (sizes, g_strdup_printf ("%zu", requisition->dims[i]));

        dimstring = join_list (sizes, " ");
        keystring = join_list (keys, ", ");

        g_print ("stat-monitor: dims=[%s] keys=[%s] location=", dimstring, keystring);

        switch (location) {
            case UFO_BUFFER_LOCATION_HOST:
                g_print ("host");
                break;
            case UFO_BUFFER_LOCATION_DEVICE:
                g_print ("device");
                break;
            case UFO_BUFFER_LOCATION_DEVICE_IMAGE:
                g_print ("image");
                break;
            case UFO_BUFFER_LOCATION_INVALID:
                g_print ("invalid");
                break;
        }

        g_print ("\n");

        g_free (dimstring);
        g_free (keystring);
        g_list_free (keys);
        g_list_free_full (sizes, (GDestroyNotify) g_free);
    }

    if (priv->n_items > 0) {
        gfloat *data_f32;

        data_f32 = (gfloat *) ufo_buffer_get_host_array (inputs[0], NULL);
        g_print ("  ");

        for (guint i = 0; i < priv->n_items; i++) {
            g_print ("%e ", data_f32[i]);

            if ((i != 0) && (((i + 1) % 8) == 0))
                g_print ("\n  ");
        }

        if ((priv->n_items % 8) != 0)
            g_print ("\n");
    }

    ufo_buffer_copy (inputs[0], output);

    return TRUE;
}

static void
ufo_stat_monitor_task_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
    UfoStatMonitorTaskPrivate *priv = UFO_STAT_MONITOR_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_NUM_ITEMS:
            priv->n_items = g_value_get_uint (value);
            break;
        case PROP_STAT_FN:
            g_free (priv->stat_fn);
            priv->stat_fn = g_value_dup_string (value);
            break;
        case PROP_TRACE:
            priv->trace_count = g_value_get_boolean (value);
            break;
        case PROP_QUIET:
            priv->be_quiet = g_value_get_boolean (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_stat_monitor_task_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
    UfoStatMonitorTaskPrivate *priv = UFO_STAT_MONITOR_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_NUM_ITEMS:
            g_value_set_uint (value, priv->n_items);
            break;
        case PROP_STAT_FN:
            g_value_set_string (value, priv->stat_fn);
            break;
        case PROP_TRACE:
            g_value_set_boolean (value, priv->trace_count);
            break;
        case PROP_QUIET:
            g_value_set_boolean (value, priv->be_quiet);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_stat_monitor_task_finalize (GObject *object)
{
    UfoStatMonitorTaskPrivate *priv = UFO_STAT_MONITOR_TASK_GET_PRIVATE (object);

    if (stdout != priv->stat_file) {
        fclose(priv->stat_file);
        priv->stat_file = NULL;
    }

    if (priv->trace_count) {
        fprintf (stdout, "stat-monitor (%u) finalising after processing %zu frames.\n",
                 priv->sm_index, priv->im_index);
    }

    g_free (priv->stat_fn);
    priv->stat_fn = NULL;

    if (priv->kernel)
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel));

    if (priv->kernel_final)
        UFO_RESOURCES_CHECK_CLERR (clReleaseKernel (priv->kernel_final));

    if (priv->stat_out_buff)
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->stat_out_buff));

    if (priv->stat_out_red)
        UFO_RESOURCES_CHECK_CLERR (clReleaseMemObject (priv->stat_out_red));

    G_OBJECT_CLASS (ufo_stat_monitor_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_stat_monitor_task_setup;
    iface->get_num_inputs = ufo_stat_monitor_task_get_num_inputs;
    iface->get_num_dimensions = ufo_stat_monitor_task_get_num_dimensions;
    iface->get_mode = ufo_stat_monitor_task_get_mode;
    iface->get_requisition = ufo_stat_monitor_task_get_requisition;
    iface->process = ufo_stat_monitor_task_process;
}

static void
ufo_stat_monitor_task_class_init (UfoStatMonitorTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_stat_monitor_task_set_property;
    oclass->get_property = ufo_stat_monitor_task_get_property;
    oclass->finalize = ufo_stat_monitor_task_finalize;

    properties[PROP_STAT_FN] =
        g_param_spec_string("filename",
            "Filename for the statistics output file.",
            "If provided with a '-' it will output statistcis to standard output of the process",
            "-",
            G_PARAM_READWRITE);

    properties[PROP_TRACE] =
        g_param_spec_boolean("trace",
            "When turned to true, will print processed frame index on stdout",
            "Defaulting to 'false', that is mimicking the 'monitor' filter",
            FALSE,
            G_PARAM_READWRITE);

    properties[PROP_QUIET] =
        g_param_spec_boolean("quiet",
            "When turned to true, will not print frame monitoring information on stdout",
            "Defaulting to 'false', that is mimicking the 'monitor' filter",
            FALSE,
            G_PARAM_READWRITE);

    properties[PROP_NUM_ITEMS] =
        g_param_spec_uint ("print",
            "Number of items to print",
            "Number of items to print",
            0, G_MAXUINT, 0,
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoStatMonitorTaskPrivate));
}

static void
ufo_stat_monitor_task_init(UfoStatMonitorTask *self)
{
    static guint sm_next_index = 0;

    self->priv = UFO_STAT_MONITOR_TASK_GET_PRIVATE(self);

    self->priv->stat_file = stdout;
    self->priv->stat_fn = g_strdup ("-");
    self->priv->trace_count = FALSE;
    self->priv->be_quiet = FALSE;
    self->priv->n_items = 0;
    self->priv->sm_index = sm_next_index++;
}
