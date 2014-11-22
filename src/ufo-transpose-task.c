/*
 * Copyright (C) 2011-2014 Karlsruhe Institute of Technology
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
#ifdef __SSE__
#include <xmmintrin.h>
#endif
#include "ufo-transpose-task.h"

/**
 * SECTION:ufo-transpose-task
 * @Short_description: Transpose images
 * @Title: transpose
 *
 */

static void ufo_task_interface_init (UfoTaskIface *iface);

G_DEFINE_TYPE_WITH_CODE (UfoTransposeTask, ufo_transpose_task, UFO_TYPE_TASK_NODE,
                         G_IMPLEMENT_INTERFACE (UFO_TYPE_TASK,
                                                ufo_task_interface_init))

#define UFO_TRANSPOSE_TASK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), UFO_TYPE_TRANSPOSE_TASK, UfoTransposeTaskPrivate))

struct _UfoTransposeTaskPrivate {
    gboolean nothing;
};

UfoNode *
ufo_transpose_task_new (void)
{
    return UFO_NODE (g_object_new (UFO_TYPE_TRANSPOSE_TASK, NULL));
}

static void
ufo_transpose_task_setup (UfoTask *task,
                          UfoResources *resources,
                          GError **error)
{
}

static void
ufo_transpose_task_get_requisition (UfoTask *task,
                                    UfoBuffer **inputs,
                                    UfoRequisition *requisition)
{
    UfoRequisition in_req;

    ufo_buffer_get_requisition (inputs[0], &in_req);

    requisition->n_dims = 2;
    requisition->dims[0] = in_req.dims[1];
    requisition->dims[1] = in_req.dims[0];
}

static guint
ufo_transpose_task_get_num_inputs (UfoTask *task)
{
    return 1;
}

static guint
ufo_transpose_task_get_num_dimensions (UfoTask *task,
                                       guint input)
{
    g_return_val_if_fail (input == 0, 0);

    return 2;
}

static UfoTaskMode
ufo_transpose_task_get_mode (UfoTask *task)
{
    return UFO_TASK_MODE_PROCESSOR;
}

static inline void
transpose_sse (gfloat *src, gfloat *dst, const int width, const int height)
{
    __m128 row1 = _mm_loadu_ps (src);
    __m128 row2 = _mm_loadu_ps (src + height);
    __m128 row3 = _mm_loadu_ps (src + 2 * height);
    __m128 row4 = _mm_loadu_ps (src + 3 * height);
    _MM_TRANSPOSE4_PS (row1, row2, row3, row4);
    _mm_storeu_ps (dst, row1);
    _mm_storeu_ps (dst + width, row2);
    _mm_storeu_ps (dst + 2 * width, row3);
    _mm_storeu_ps (dst + 3 * width, row4);
}

static gboolean
ufo_transpose_task_process (UfoTask *task,
                            UfoBuffer **inputs,
                            UfoBuffer *output,
                            UfoRequisition *requisition)
{
    gfloat *host_array;
    gfloat *transposed;
    guint width = requisition->dims[0];
    guint height = requisition->dims[1];
    guint fast_width = width - width % 4;
    guint fast_height = height - height % 4;
    const guint block_size = 128;

    host_array = ufo_buffer_get_host_array (inputs[0], NULL);
    transposed = ufo_buffer_get_host_array (output, NULL);

    #ifdef __SSE__
    /* Use SSE to do a 4x4 micro transposition, execute such transpositions in a
     * block-based fashion and last, execute as many blocks as fit the image
     * dimensions reduced by modulo 4 outliers. */
    #pragma omp parallel for
    for (guint j = 0; j < fast_height; j+=block_size) {
        guint block_j = j + block_size < fast_height ? j + block_size : fast_height;
        for (guint i = 0; i < fast_width; i+=block_size) {
            guint block_i = i + block_size < fast_width ? i + block_size : fast_width;
            for (guint l = j; l < block_j; l+=4) {
                for (guint k = i; k < block_i; k+=4) {
                    transpose_sse (host_array + k * height + l, transposed + l * width + k, width, height);
                }
            }
        }
    }

    /* Finish the outliers which exceed block size. This loop cannot be executed
     * in parallel to the previous one, otherwise the vector access and outlier
     * access might happen at the same time producing invalid results. */
    #pragma omp parallel for
    for (guint j = 0; j < height; j++) {
        /* If we are in the height which was processed in a vectorized way treat
         * only the x outlier, otherwise the whole row has to be processed. */
        guint start_i = j < fast_height ? fast_width : 0;
        for (guint i = start_i; i < width; i++) {
            transposed[j * width + i] = host_array[i * height + j];
        }
    }
    #else
    /* Transpose in blocks */
    #pragma omp parallel for
    for (guint j = 0; j < height; j+=block_size) {
        guint block_j = j + block_size < height ? j + block_size : height;
        for (guint i = 0; i < width; i+=block_size) {
            guint block_i = i + block_size < width ? i + block_size : width;
            for (guint l = j; l < block_j; l++) {
                for (guint k = i; k < block_i; k++) {
                    transposed[l * width + k] = host_array[k * height + l];
                }
            }
        }
    }
    #endif

    return TRUE;
}

static void
ufo_transpose_task_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ufo_transpose_task_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
ufo_transpose_task_finalize (GObject *object)
{
    G_OBJECT_CLASS (ufo_transpose_task_parent_class)->finalize (object);
}

static void
ufo_task_interface_init (UfoTaskIface *iface)
{
    iface->setup = ufo_transpose_task_setup;
    iface->get_num_inputs = ufo_transpose_task_get_num_inputs;
    iface->get_num_dimensions = ufo_transpose_task_get_num_dimensions;
    iface->get_mode = ufo_transpose_task_get_mode;
    iface->get_requisition = ufo_transpose_task_get_requisition;
    iface->process = ufo_transpose_task_process;
}

static void
ufo_transpose_task_class_init (UfoTransposeTaskClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS (klass);

    oclass->set_property = ufo_transpose_task_set_property;
    oclass->get_property = ufo_transpose_task_get_property;
    oclass->finalize = ufo_transpose_task_finalize;

    g_type_class_add_private (oclass, sizeof(UfoTransposeTaskPrivate));
}

static void
ufo_transpose_task_init(UfoTransposeTask *self)
{
    self->priv = UFO_TRANSPOSE_TASK_GET_PRIVATE(self);
}
