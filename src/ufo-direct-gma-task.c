/*
 * Copyright (C) 2011-2015 Karlsruhe Institute of Technology
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
#include "ufo-direct-gma-task.h"

#ifdef __APPLE__
/* go fuck people with apple*/
#else
#include "CL/cl.h"
#include "CL/cl_ext.h" 
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>

#define PAGE_SIZE 4096
#define NB_PAGES 8000
#define NB_SAVING_BUFFERS(X) ((X)/(PAGE_SIZE*NB_PAGES))

struct _UfoDirectGmaTaskPrivate {
    gboolean foo;
    guint width;
    guint height;
    guint depth;
    guint bitdepth;
    guint frames;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoDirectGmaTask, ufo_direct_gma_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_DIRECT_GMA_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_DIRECT_GMA_TASK, UfoDirectGmaTaskPrivate))

enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FRAMES,
    PROP_DEPTH,
    PROP_BITDEPTH,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

UfoNode *
ufo_direct_gma_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_DIRECT_GMA_TASK, NULL));
}

static void
ufo_direct_gma_task_setup (UfoTask *task,
                       UfoResources *resources,
                       GError **error)
{
    UfoDirectGmaTaskPrivate *priv;
    priv = UFO_DUMMY_DATA_TASK_GET_PRIVATE (task);
    priv->current = 0;
}

static void
ufo_direct_gma_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
  UfoDummyDataTaskPrivate *priv;

    priv = UFO_DUMMY_DATA_TASK_GET_PRIVATE (task);

    requisition->n_dims = 2;
    requisition->dims[0] = priv->width;
    requisition->dims[1] = priv->height;

     if (priv->depth > 2) {
        requisition->n_dims += 1;
        requisition->dims[2] = priv->depth;
    }
}

static guint
ufo_direct_gma_task_get_num_inputs (UfoTask *task)
{
    return 0;
}

static guint
ufo_direct_gma_task_get_num_dimensions (UfoTask *task,
                                             guint input)
{
    return 0;
}

static UfoTaskMode
ufo_direct_gma_task_get_mode (UfoTask *task)
{
  return UFO_TASK_MODE_GENERATOR | UFO_TASK_MODE_GPU ;
}

static gboolean
ufo_direct_gma_task_generate (UfoTask *task,
                              UfoBuffer *output,
                         UfoRequisition *requisition)
{
  /* initialize the buffers for directGMA*/
  
    UfoGpuNode *node;

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    cmd_queue = ufo_gpu_node_get_cmd_queue (node);
    
    UfoBuffer priv1,*priv2;
    guint total_number_saving_buffers;
    guint i;
    gulong total_data_size;
    gint *test;

    total_data_size = task->priv->width * task->priv->height * task->priv->depth * task->priv->bitdepth * task->priv->frames;

    priv1.location = UFO_BUFFER_LOCATION_DEVICE_DIRECT_GMA;
    priv1.size=PAGE_SIZE*NB_PAGES;
    if((test=ufo_buffer_get_device_array(priv1,cmd_queue)) == NULL) return FALSE;
    
    if((total_data_size%(PAGE_SIZE*NB_PAGES)) == 0) total_number_saving_buffers = NB_SAVING_BUFFERS(total_data_size);
    else if ((total_data_size%(PAGE_SIZE*NB_PAGES)) != 0) total_number_saving_buffers = NB_SAVING_BUFFERS(total_data_size)+1 ;
    
    priv2=malloc(total_number_saving_buffers*PAGE_SIZE*NB_PAGES*sizeof(char));
    for(i=0; i<total_number_saving_buffers;i++){
        priv2[i].location = UFO_BUFFER_LOCATION_DEVICE;
	priv2[i].size=PAGE_SIZE*NB_PAGES;
	if((test=ufo_buffer_get_device_array(priv2[i],cmd_queue)) == NULL) return FALSE;
    }
    
    /*get synchronization resources, may use futex if root*/
    sem_t *sem_operation, *sem_shm, *sem_init;
    sem_operation=sem_open("sem_operation",O_RDWR | O_CREAT,0600,1);
    sem_shm=sem_open("sem_shm",O_RDWR | O_CREAT,0600,1); /**<in fact should we use it, and if yes, here or direclty in core (normally core should be the way, but we have no clue about fpga part*/
    sem_init=sem_open("sem_init",O_RDWR | O_CREAT,0600,1);


    /*make the transfer in the following*/
    /* first wait for initialization from fpga part*/
    sem_wait(sem_init);
    sem_post(sem_init);
    
    /* we utilize the simple buffering technique for now, as the double one is not ready*/
    for(i=0;i<total_number_saving_buffers;i++){
        sem_wait(sem_operation);
	ufo_buffer_copy(priv1,priv2[i]);
	sem_post(sem_operation);
    }
    
    /*cleaning*/
    sem_close(sem_init);
    sem_close(sem_operation);
    sem_close(sem_shm);
    sem_unlink("sem_shm");
    sem_unlink("sem_operation");
    sem_unlink("sem_shm");

    return TRUE;
}

static void
ufo_direct_gma_task_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
    UfoDirectGmaTaskPrivate *priv = UFO_DIRECT_GMA_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_WIDTH:
            priv->width = g_value_get_uint (value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint (value);
            break;
        case PROP_DEPTH:
            priv->depth = g_value_get_uint (value);
            break;
        case PROP_FRAMES:
            priv->frames = g_value_get_uint (value);
            break;
        case PROP_BITDEPTH:
            {
                guint depth;

                depth = g_value_get_uint (value);

                if (depth != 8 && depth != 16 && depth != 32)
                    g_warning ("::bitdepth must be either 8, 16 or 32");
                else
                    priv->bitdepth = depth;
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_direct_gma_task_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    UfoDirectGmaTaskPrivate *priv = UFO_DIRECT_GMA_TASK_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_WIDTH:
            priv->width = g_value_get_uint (value);
            break;
        case PROP_FRAMES:
            priv->frames = g_value_get_uint(value);
            break;
        case PROP_HEIGHT:
            priv->height = g_value_get_uint (value);
            break;
        case PROP_FRAMES:
	    priv->frames= g_value_get_uint(value);
	    break;
        case PROP_BITDEPTH:
            {
                guint depth;

                depth = g_value_get_uint (value);

                if (depth != 8 && depth != 16 && depth != 32)
                    g_warning ("::bitdepth must be either 8, 16 or 32");
                else
                    priv->bitdepth = depth;
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
ufo_direct_gma_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_direct_gma_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_direct_gma_task_setup;
    iface->get_num_inputs = ufo_direct_gma_task_get_num_inputs;
    iface->get_num_dimensions = ufo_direct_gma_task_get_num_dimensions;
    iface->get_mode = ufo_direct_gma_task_get_mode;
    iface->get_requisition = ufo_direct_gma_task_get_requisition;
    iface->process = ufo_direct_gma_task_process;
}

static void
ufo_direct_gma_task_class_init (UfoDirectGmaTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_direct_gma_task_set_property;
    oclass->get_property = ufo_direct_gma_task_get_property;
    oclass->finalize = ufo_direct_gma_task_finalize;

    properties[PROP_TEST] =
        g_param_spec_string ("test",
            "Test property nick",
            "Test property description blurb",
            "",
            G_PARAM_READWRITE);

    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoDirectGmaTaskPrivate));
}

static void
ufo_direct_gma_task_init(UfoDirectGmaTask *self)
{
    self->priv = UFO_DIRECT_GMA_TASK_GET_PRIVATE(self);
    self->priv->width = 1;
    self->priv->height = 1;
    self->priv->depth = 1;
    self->priv->frames = 1;
    self->priv->current = 0;
    self->priv->bitdepth = 32;
}
