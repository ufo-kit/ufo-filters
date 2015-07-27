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
#define _POSIX_C_SOURCE 200809L
#define _BSD_SOURCE
#define _XOPEN_SOURCE 700
#include "ufo-direct-gma-task.h"

#ifdef __APPLE__
/* go fuck people with apple*/
#else
#include "CL/cl.h"
#include "CL/cl_ext.h" 
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sched.h>
#include <errno.h>

#include <pcilib.h>
#include <pcilib/kmem.h>
#include <pcilib/bar.h>
#include <pcilib/error.h>
#include <sys/mman.h>
#include <fcntl.h>

#define DEVICE "/dev/fpga0"

#define IPECAMERA
#define BAR PCILIB_BAR0
#define USE_RING PCILIB_KMEM_USE(PCILIB_KMEM_USE_USER, 1)
#define USE PCILIB_KMEM_USE(PCILIB_KMEM_USE_USER, 2)

#define BUFFERS         4 
#define ITERATIONS      1 
#define DESC_THRESHOLD  1

#define PAGE_SIZE       4096        // other values are not supported in the kernel

#define USE_64                     
#define USE_STREAMING

#define ENABLE_COUNTER  0x9000 

#define FPGA_CLOCK 250

#define WR(addr, value) { *(uint32_t*)(bar + addr + offset) = value; }
#define RD(addr, value) { value = *(uint32_t*)(bar + addr + offset); }

struct _UfoDirectGmaTaskPrivate {
    gboolean foo;
    guint huge_page;
    guint tlp_size;
    guint multiple;
    cl_context context;
    cl_platform_id platform_id;
};

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoDirectGmaTask, ufo_direct_gma_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_DIRECT_GMA_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_DIRECT_GMA_TASK, UfoDirectGmaTaskPrivate))

enum {
    PROP_0,
    PROP_HUGE_PAGE,
    PROP_TLP_SIZE,
    PROP_MULTIPLE,
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
    priv = UFO_DIRECT_GMA_TASK_GET_PRIVATE (task);

    priv->context= ufo_resources_get_context(resources);
    priv->platform_id= ufo_get_platform_id_for_directgma(resources);
}

static void
ufo_direct_gma_task_get_requisition (UfoTask *task,
                                 UfoBuffer **inputs,
                                 UfoRequisition *requisition)
{
    requisition->n_dims = 1;
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

static int
gpu_init( glong* address_buffer_1, glong* address_buffer_2, UfoBuffer *saving_buffers, UfoBuffer* buffer_amd1, UfoBuffer* buffer_amd2, cl_command_queue* command_queue, UfoTask* task){
    cl_bus_address_amd busadress, busadress2;
    long i;

    UfoGpuNode *node;
    UfoDirectGmaTaskPrivate *task_priv;
    task_priv= UFO_DIRECT_GMA_TASK_GET_PRIVATE(task); 
    
    memset(&busadress,0,sizeof(cl_bus_address_amd));
    memset(&busadress2,0,sizeof(cl_bus_address_amd));

    node = UFO_GPU_NODE (ufo_task_node_get_proc_node (UFO_TASK_NODE (task)));
    *command_queue = ufo_gpu_node_get_cmd_queue (node);
   
    for(i=0;i<task_priv->multiple;i++){
        saving_buffers[i]=*(UfoBuffer*)ufo_buffer_new_with_size_in_bytes(1024*task_priv->huge_page*sizeof(int), task_priv->context);
        ufo_buffer_set_location(&saving_buffers[i],UFO_BUFFER_LOCATION_DEVICE);
    	ufo_buffer_get_device_array(&saving_buffers[i],command_queue);
    }
    
    *buffer_amd1=*(UfoBuffer*)ufo_buffer_new_with_size_in_bytes(1024*task_priv->huge_page*sizeof(int), task_priv->context);
    ufo_buffer_set_location(buffer_amd1,UFO_BUFFER_LOCATION_DEVICE_DIRECT_GMA);
    ufo_buffer_get_device_array_for_directgma(buffer_amd1,command_queue,task_priv->platform_id,&busadress);
    
    *buffer_amd2=*(UfoBuffer*)ufo_buffer_new_with_size_in_bytes(1024*task_priv->huge_page*sizeof(int), task_priv->context);
    ufo_buffer_set_location(buffer_amd2,UFO_BUFFER_LOCATION_DEVICE_DIRECT_GMA);
    ufo_buffer_get_device_array_for_directgma(buffer_amd2,command_queue, task_priv->platform_id, &busadress2);
    
    *address_buffer_1=busadress.surface_bus_address;
    *address_buffer_2=busadress2.surface_bus_address;

    return 0;
}    

static void
pcie_test(volatile void* bar){
    uintptr_t offset = 0;
    int err;
    printf("* DMA: Reset...\n");
    WR(0x00, 0x1);
    usleep(100000);
    WR(0x00, 0x0);
    usleep(100000);


    printf("* PCIe: Testing...");
    RD(0x0, err);
    if (err == 335746816 || err == 335681280) {
       printf("\xE2\x9C\x93 \n");
    } else {
       printf("\xE2\x9C\x98\n PCIe not ready!\n");
       exit(0);
    }
}


static void
dma_conf(volatile void* bar, UfoDirectGmaTaskPrivate* task_priv){
 uintptr_t offset = 0;
    printf("* DMA: Send Data Amount\n");
    WR(0x10, (task_priv->huge_page * (PAGE_SIZE / (4 * task_priv->tlp_size))));                              
    printf("* DMA: Running mode: ");

#ifdef USE_64   
    if (task_priv->tlp_size == 64)
    {
        WR(0x0C, 0x80040);
        printf ("64bit - 256B Payload\n");
    }
    else if (task_priv->tlp_size == 32)
    {
        WR(0x0C, 0x80020);
        printf ("64bit - 128B Payload\n");
    }
#else  
    if (task_priv->tlp_size == 64) 
    {
        WR(0x0C, 0x0040);
        printf ("32bit - 256B Payload\n");
    }
    else if (task_priv->tlp_size == 32) 
    {
        WR(0x0C, 0x0020);
        printf ("32bit - 128B Payload\n");
    }
#endif
    
    printf("* DMA: Reset Desc Memory...\n");
    WR(0x5C, 0x00); 
}


static void
pcilib_init_for_transfer(pcilib_t** pci, uintptr_t* kdesc_bus,volatile uint32_t** desc, volatile void** bar){
    pcilib_kmem_handle_t *kdesc;
    pcilib_kmem_flags_t flags = PCILIB_KMEM_FLAG_HARDWARE|PCILIB_KMEM_FLAG_PERSISTENT|PCILIB_KMEM_FLAG_EXCLUSIVE;
    pcilib_kmem_flags_t clean_flags = PCILIB_KMEM_FLAG_HARDWARE|PCILIB_KMEM_FLAG_PERSISTENT|PCILIB_KMEM_FLAG_EXCLUSIVE;
    pcilib_bar_t bar_tmp = BAR;
    uintptr_t offset = 0;

    *pci = pcilib_open(DEVICE, "pci");
    if (!(*pci)) pcilib_error("pcilib_open");
  
    *bar = pcilib_map_bar(*pci, BAR);
    if (!(*bar)) {
       pcilib_close(*pci);
       pcilib_error("map bar");
    }

    pcilib_detect_address(*pci, &bar_tmp, &offset, 1);

    pcilib_enable_irq(*pci, PCILIB_IRQ_TYPE_ALL, 0);
    pcilib_clear_irq(*pci, PCILIB_IRQ_SOURCE_DEFAULT);

    pcilib_clean_kernel_memory(*pci, USE, clean_flags);
    pcilib_clean_kernel_memory(*pci, USE_RING, clean_flags);

    kdesc = pcilib_alloc_kernel_memory(*pci, PCILIB_KMEM_TYPE_CONSISTENT, 1, 128, 4096, USE_RING, flags);
    *kdesc_bus = pcilib_kmem_get_block_ba(*pci, kdesc, 0);
    *desc = (uint32_t*)pcilib_kmem_get_block_ua(*pci, kdesc, 0);
    memset((void*)*desc, 0, 5*sizeof(uint32_t));

}

static void
writing_dma_descriptors(glong buffer_gma_addr1, glong buffer_gma_addr2, uintptr_t kdesc_bus,volatile void*bar, uintptr_t *bus_addr){
    uintptr_t offset = 0;
    gint j;
    printf("Writing SW Read Descriptor\n");
    WR(0x58, BUFFERS-1);

    printf("Writing the Descriptor Threshold\n");
    WR(0x60, DESC_THRESHOLD);

    printf("Writing HW write Descriptor Address: %lx\n", kdesc_bus);
    WR(0x54, kdesc_bus);
    usleep(100000);

    printf("* DMA: Writing Descriptors\n");
    bus_addr[0] = buffer_gma_addr1;
    usleep(1000);
    bus_addr[1] = buffer_gma_addr2;
    usleep(1000);
    bus_addr[2] = buffer_gma_addr1;
    usleep(1000);
    bus_addr[3] = buffer_gma_addr2;
    usleep(1000);
    for (j = 0; j < BUFFERS; j++ ) {
        printf("Writing descriptor num. %i: \t %08lx \n", j, bus_addr[j]);
        WR(0x50, bus_addr[j]);
    }


}

static void
handshaking_dma(UfoBuffer* buffer_gma1,UfoBuffer* buffer_gma2, UfoBuffer* saving_buffers, volatile uint32_t *desc,volatile void* bar, cl_command_queue* command_queue , uintptr_t* bus_addr, gint* iterations_completed,UfoDirectGmaTaskPrivate* task_priv){
 uint32_t curptr, hwptr;
 uint32_t curbuf;
 gint i,j;
 guint y;
 uintptr_t offset = 0;
 
 for(y=0;y<(task_priv->multiple/4);y++){
//    printf("boucle %i\n",y);
    i=0;
    curptr=0;
    curbuf=0;
    while (i < ITERATIONS) {
        j = 0;
        do {
#ifdef USE_64   
                hwptr = desc[3];
#else // 32-bit
                hwptr = desc[4];
#endif
        j++;    
    //    printf("\rcurptr: %lx \t \t hwptr: %lx", curptr, hwptr);
        } while (hwptr == curptr);

        do {    
  //  printf("inside loop: %i\n",curbuf);        
	  if((curbuf%2)==0 && curbuf!=0) ufo_buffer_copy_for_directgma(buffer_gma2,&(saving_buffers[y*4+curbuf-1]), command_queue);
          else if((curbuf%2)==1) ufo_buffer_copy_for_directgma(buffer_gma1,&(saving_buffers[y*4+curbuf-1]),command_queue);


#ifdef USE_STREAMING
		WR(0x50, bus_addr[curbuf]);
        // Now, we can check error in the register 0x68 (non-zero if there is an error)
#endif /* USE_STREAMING */
            
            curbuf++;
            if (curbuf == BUFFERS) {
                i++;
                curbuf = 0;
                if (i >= ITERATIONS) break;
            }
        } while (bus_addr[curbuf] != hwptr);

#ifndef USE_STREAMING
	    // Generally it is safe to write register in any case, it is just unsed in streaming mode
	    // Therefore, in the driver we can use it instead of software register so far
        WR(0x58, curbuf + 1); 
#endif 
        curptr = hwptr;
    }
  ufo_buffer_copy_for_directgma(buffer_gma2,&(saving_buffers[(y+1)*4-1]), command_queue);
  }
  *iterations_completed   = ITERATIONS;
}

static void
stop_dma(struct timeval *end,gfloat* perf_counter,volatile void* bar){
 uintptr_t offset = 0;
 gettimeofday(end, NULL);
    printf("* DMA :Stop\n");
    WR(0x04, 0x00);
    usleep(100);
    RD(0x28, *perf_counter);
    usleep(100);
    WR(0x00, 0x01);
}

static void
free_and_close(pcilib_t *pci,volatile void* bar,UfoBuffer buffer_gma1, UfoBuffer buffer_gma2, uintptr_t* bus_addr, UfoDirectGmaTaskPrivate* task_priv){
    free(bus_addr);
    /*ufo_buffer_finalize(&buffer_gma1); 
    ufo_buffer_finalize(&buffer_gma2);*/
    pcilib_disable_irq(pci, 0);
    pcilib_unmap_bar(pci, BAR, bar);
    pcilib_close(pci);
}

static void
start_dma(volatile void* bar,struct timeval *start){
     uintptr_t offset = 0;
     printf("* DMA: Start \n");
     WR(0x04, 0x1);

#ifdef IPECAMERA
     WR(0x9040, 0x88000201);
     WR(0x9100, 0x00001000);
#endif 

#ifdef ENABLE_COUNTER
     printf("* Enable counter\n");
     WR(ENABLE_COUNTER, 0x1);
#endif 
     gettimeofday(start, NULL);
}

static void
perf( struct timeval start, struct timeval end, float perf_counter, int iterations_completed, UfoDirectGmaTaskPrivate* task_priv){

     long long int size_mb;
     size_t run_time;
     float performance;

     printf("Iterations done: %d\n", iterations_completed);
//    printf("end usec %zu start usec %zu",end.tv_usec,start.tv_usec);
     run_time = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
     size_mb=(task_priv->multiple*task_priv->huge_page*4096)/1000000*iterations_completed;
     printf("Performance: transfered %llu Mbytes in %zu us using %d buffers\n", (size_mb), run_time, BUFFERS);
     performance = ((size_mb * FPGA_CLOCK * 1000000)/(perf_counter*256));
     printf("DMA perf counter:\t%d\n", (int)perf_counter); 
     printf("DMA side:\t\t%.3lf MB/s\n", performance);  
     printf("PC side:\t\t%.3lf MB/s\n\n", 1000000. * size_mb / run_time );
}

static void
print_results(cl_command_queue* cmd_queue, UfoBuffer* buffer, UfoDirectGmaTaskPrivate* task_priv){
    int* results;
    /* change here if needed*/
    results=malloc(task_priv->huge_page*1024*sizeof(int));
    ufo_buffer_read(buffer,results,cmd_queue);
    int i;
    for(i=0; i<200; i++) printf("%i| ", results[i]);
}

static gboolean
ufo_direct_gma_task_generate (UfoTask *task,
                              UfoBuffer *output,
                         UfoRequisition *requisition)
{
  struct timeval start;
  struct timeval end;
    gfloat perf_counter;
    gint iterations_completed;

    pcilib_t *pci=NULL;
    uintptr_t kdesc_bus;
    volatile uint32_t *desc=NULL;
    
    glong buffer_gma_addr1,buffer_gma_addr2;
    UfoBuffer buffer_gma1,buffer_gma2;
    cl_command_queue command_queue;
    volatile void* bar=NULL;
    uintptr_t* bus_addr;

    UfoDirectGmaTaskPrivate *task_priv;
    task_priv= UFO_DIRECT_GMA_TASK_GET_PRIVATE(task);
    
    bus_addr=malloc(BUFFERS*sizeof(uintptr_t));
    output=malloc((task_priv->multiple)*sizeof(UfoBuffer));

    gpu_init(&buffer_gma_addr1, &buffer_gma_addr2, output, &buffer_gma1, &buffer_gma2, &command_queue, task);
    
    pcilib_init_for_transfer(&pci,&kdesc_bus,&desc,&bar);

    pcie_test(bar);

    dma_conf(bar, task_priv);
   
    writing_dma_descriptors(buffer_gma_addr1,buffer_gma_addr2, kdesc_bus,bar, bus_addr);
  
    start_dma(bar, &start);

    handshaking_dma(&buffer_gma1, &buffer_gma2, output, desc,bar,&command_queue,bus_addr,&iterations_completed, task_priv);
    
    stop_dma(&end, &perf_counter,bar);

    perf(start,end,perf_counter,iterations_completed, task_priv);

    print_results(&command_queue,&output[0], task_priv);

    free_and_close(pci,bar,buffer_gma1,buffer_gma2,bus_addr,task_priv);
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
        case PROP_HUGE_PAGE:
	    priv->huge_page=g_value_get_uint(value);
            break;
        case PROP_TLP_SIZE:
            {
	        guint size;
                size=g_value_get_uint(value);
                if(size != 32 && size !=64)
                    g_warning("tlp size can be 32 or 64,and must be correct according the results of lspci command");
                else
	            priv->tlp_size=size;
            }
            break;
        case PROP_MULTIPLE:
            {
	        guint factor;
	        factor=g_value_get_uint(value);
        	if((factor%4)!=0 || factor==0)
	            g_warning("multiple must be at least 4 and is then a multiple of 4");
        	else 
	            priv->multiple=factor;
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
<<<<<<< HEAD
        case PROP_HUGE_PAGE:
	    g_value_set_uint(value,priv->huge_page);
            break;
        case PROP_TLP_SIZE:
            g_value_set_uint(value,priv->tlp_size);
            break;
        case PROP_MULTIPLE:
	  g_value_set_uint(value,priv->multiple);
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
    iface->generate = ufo_direct_gma_task_generate;
}

static void
ufo_direct_gma_task_class_init (UfoDirectGmaTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_direct_gma_task_set_property;
    oclass->get_property = ufo_direct_gma_task_get_property;
    oclass->finalize = ufo_direct_gma_task_finalize;

    properties[PROP_HUGE_PAGE]=
        g_param_spec_uint("huge-page",
			  "number of pages of 4k in one dma buffer",
			  "number of pages of 4k in one dma buffer",
			  1,2000,2000,
			  G_PARAM_READWRITE);

    /* here the maximum size transferred is aroud 3.5e16 bytes, that should be enough, but may change the uint property of it*/
    properties[PROP_MULTIPLE]=
        g_param_spec_uint("multiple",
			  "represents the number of virtual buffers used for dma",
			  "represents the number of virtual buffers used for dma",
			  4,0xffff,4,
              G_PARAM_READWRITE);

    properties[PROP_TLP_SIZE]=
        g_param_spec_uint("tlp-size",
			  "size for the corresponding payload size of pcie frame",
			  "size for the corresponding payload size of pcie frame",
			  32,64,32,
              G_PARAM_READWRITE);
    for (guint i = PROP_0 + 1; i < N_PROPERTIES; i++)
        g_object_class_install_property (oclass, i, properties[i]);

    g_type_class_add_private (oclass, sizeof(UfoDirectGmaTaskPrivate));
}

static void
ufo_direct_gma_task_init(UfoDirectGmaTask *self)
{
    self->priv = UFO_DIRECT_GMA_TASK_GET_PRIVATE(self);
    self->priv->tlp_size=32;
    self->priv->huge_page=2000;
    self->priv->multiple=4;
}
