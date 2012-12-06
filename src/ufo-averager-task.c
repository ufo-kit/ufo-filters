/**
 * SECTION:ufo-averager-task
 * @Short_description: Write TIFF files
 * @Title: averager
 *
 * The averager node writes each incoming image as a TIFF using libtiff to disk.
 * Each file is prefixed with #UfoAveragerTask:prefix and written into
 * #UfoAveragerTask:path.
 */

#include <glib-object.h>
#include <gmodule.h>
#include <tiffio.h>
#include <ufo-cpu-task-iface.h>
#include "ufo-averager-task.h"

struct _UfoAveragerTaskPrivate {
    guint counter;
};

static void ufo_task_interface_init (UfoTaskIface *iface);
static void ufo_cpu_task_interface_init (UfoCpuTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoAveragerTask, ufo_averager_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init)
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_CPU_TASK,
                                                ufo_cpu_task_interface_init))

#define UFO_AVERAGER_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_AVERAGER_TASK, UfoAveragerTaskPrivate))

UfoNode *
ufo_averager_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_AVERAGER_TASK, NULL));
}

static void
ufo_averager_task_setup (UfoTask *task,
                         UfoResources *resources,
                         GError **error)
{
}

static void
ufo_averager_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    ufo_buffer_get_requisition (inputs[0], requisition);
}

static void
ufo_averager_task_get_structure (UfoTask *task,
                               guint *n_inputs,
                               guint **n_dims,
                               UfoTaskMode *mode)
{
    *mode = UFO_TASK_MODE_REDUCE;
    *n_inputs = 1;
    *n_dims = g_new0 (guint, 1);
    (*n_dims)[0] = 2;
}

static gboolean
ufo_averager_task_process (UfoCpuTask *task,
                           UfoBuffer **inputs,
                           UfoBuffer *output,
                           UfoRequisition *requisition)
{
    UfoAveragerTaskPrivate *priv;
    gfloat *in_array;
    gfloat *out_array;
    gsize n_pixels;

    priv = UFO_AVERAGER_TASK_GET_PRIVATE (UFO_AVERAGER_TASK (task));
    n_pixels = requisition->dims[0] * requisition->dims[1];
    in_array = ufo_buffer_get_host_array (inputs[0], NULL);
    out_array = ufo_buffer_get_host_array (output, NULL);

    for (gsize i = 0; i < n_pixels; i++)
        out_array[i] += in_array[i];

    priv->counter++;
    return TRUE;
}

static void
ufo_averager_task_reduce (UfoCpuTask *task,
                          UfoBuffer *output,
                          UfoRequisition *requisition)
{
    UfoAveragerTaskPrivate *priv;
    gfloat *out_array;
    gsize n_pixels;

    priv = UFO_AVERAGER_TASK_GET_PRIVATE (UFO_AVERAGER_TASK (task));
    n_pixels = requisition->dims[0] * requisition->dims[1];
    out_array = ufo_buffer_get_host_array (output, NULL);

    for (gsize i = 0; i < n_pixels; i++)
        out_array[i] /= (gfloat) priv->counter;
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_averager_task_setup;
    iface->get_structure = ufo_averager_task_get_structure;
    iface->get_requisition = ufo_averager_task_get_requisition;
}

static void
ufo_cpu_task_interface_init (UfoCpuTaskIface *iface)
{
    iface->process = ufo_averager_task_process;
    iface->reduce = ufo_averager_task_reduce;
}

static void
ufo_averager_task_class_init (UfoAveragerTaskClass *klass)
{
    g_type_class_add_private (G_OBJECT_CLASS (klass), sizeof(UfoAveragerTaskPrivate));
}

static void
ufo_averager_task_init(UfoAveragerTask *self)
{
    self->priv = UFO_AVERAGER_TASK_GET_PRIVATE(self);
    self->priv->counter = 0;
}
