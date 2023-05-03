/*
 * Copyright (C) 2015-2016 Karlsruhe Institute of Technology
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

#include "config.h"

#include <string.h>

#ifdef HAVE_AMD
#include <clFFT.h>
#else
#include "oclFFT.h"
#endif

#include "ufo-fft.h"

struct _UfoFft {
    UfoFftParameter seen;

#ifdef HAVE_AMD
    clfftPlanHandle amd_plan;
    clfftSetupData amd_setup;
#else
    clFFT_Plan apple_plan;
#endif
};

#ifdef HAVE_AMD
static GMutex amd_mutex;
static GList *ffts_created = NULL;
#endif


UfoFft *
ufo_fft_new (void)
{
    UfoFft *fft;

    fft = g_malloc0 (sizeof (UfoFft));

#ifdef HAVE_AMD
    UFO_RESOURCES_CHECK_CLERR (clfftSetup (&fft->amd_setup));

    g_mutex_lock (&amd_mutex);
    ffts_created = g_list_append (ffts_created, fft);
    g_mutex_unlock (&amd_mutex);
    g_debug ("INFO Create new plan using AMD FFT");
#else
    g_debug ("INFO Create new plan using Apple FFT");
#endif

    return fft;
}

cl_int
ufo_fft_update (UfoFft *fft, cl_context context, cl_command_queue queue, UfoFftParameter *param)
{
    gboolean changed;
    cl_int error;

    error = CL_SUCCESS;
    changed = param->size[0] != fft->seen.size[0] || param->size[1] != fft->seen.size[1];

    if (changed)
        memcpy (&fft->seen, param, sizeof (UfoFftParameter));

#ifdef HAVE_AMD
    if (fft->amd_plan == 0 || changed) {
        /* we use param->dimension to index into this array! */
        clfftDim dimension[4] = { 0, CLFFT_1D, CLFFT_2D, CLFFT_3D };

        if (fft->amd_plan != 0) {
            clfftDestroyPlan (&fft->amd_plan);
            fft->amd_plan = 0;
        }

        UFO_RESOURCES_CHECK_CLERR (clfftCreateDefaultPlan (&fft->amd_plan, context, dimension[param->dimensions], param->size));
        UFO_RESOURCES_CHECK_CLERR (clfftSetPlanBatchSize (fft->amd_plan, param->batch));
        UFO_RESOURCES_CHECK_CLERR (clfftSetPlanPrecision (fft->amd_plan, CLFFT_SINGLE));
        UFO_RESOURCES_CHECK_CLERR (clfftSetLayout (fft->amd_plan, CLFFT_COMPLEX_INTERLEAVED, CLFFT_COMPLEX_INTERLEAVED));
        UFO_RESOURCES_CHECK_CLERR (clfftSetResultLocation (fft->amd_plan, param->zeropad ? CLFFT_INPLACE : CLFFT_OUTOFPLACE));
        UFO_RESOURCES_CHECK_CLERR (clfftBakePlan (fft->amd_plan, 1, &queue, NULL, NULL));
    }
#else
    if (fft->apple_plan == NULL || changed) {
        clFFT_Dim3 size;

        /* we use param->dimension to index into this array! */
        clFFT_Dimension dimension[4] = { 0, clFFT_1D, clFFT_2D, clFFT_3D };

        size.x = param->size[0];
        size.y = param->size[1];
        size.z = param->size[2];

        if (fft->apple_plan != NULL) {
            clFFT_DestroyPlan (fft->apple_plan);
            fft->apple_plan = NULL;
        }

        fft->apple_plan = clFFT_CreatePlan (context, size, dimension[param->dimensions], clFFT_InterleavedComplexFormat, &error);
    }
#endif

    return error;
}

cl_int
ufo_fft_execute (UfoFft *fft, cl_command_queue queue, UfoProfiler *profiler,
                 cl_mem in_mem, cl_mem out_mem, UfoFftDirection direction,
                 cl_uint num_events, cl_event *event_list, cl_event *event)
{
#ifdef HAVE_AMD
    return clfftEnqueueTransform (fft->amd_plan,
                                  direction == UFO_FFT_FORWARD ? CLFFT_FORWARD : CLFFT_BACKWARD,
                                  1, &queue,
                                  num_events, event_list, event, &in_mem, &out_mem, NULL);
#else
    return clFFT_ExecuteInterleaved_Ufo (queue, fft->apple_plan,
                                         fft->seen.batch,
                                         direction == UFO_FFT_FORWARD ? clFFT_Forward : clFFT_Inverse,
                                         in_mem, out_mem, num_events, event_list, event, profiler);
#endif
}

void
ufo_fft_destroy (UfoFft *fft)
{
#ifdef HAVE_AMD
    g_mutex_lock (&amd_mutex);

    clfftDestroyPlan (&fft->amd_plan);
    ffts_created = g_list_remove (ffts_created, fft);

    if (g_list_length (ffts_created) == 0)
        clfftTeardown ();

    g_mutex_unlock (&amd_mutex);
#else
    clFFT_DestroyPlan (fft->apple_plan);
#endif

    g_free (fft);
}

/** ufo_fft_chirp_z:
 *
 * @fft: #UfoFft
 * @param: #UfoFftParameter
 * @queue: %cl_command_queue
 * @profiler: #UfoProfiler
 * @in_mem: %cl_mem
 * @tmp_mem: %cl_mem for temporary large FFT
 * @out_mem: %cl_mem for final FT result
 * @coeffs_buffer: #UfoBuffer holding Chirp-z coefficients
 * @f_coeffs_buffer: #UfoBuffer holding Fourier-transformed conjugated Chirp-z coefficients
 * @coeffs_kernel: %cl_kernel for computing Chirp-z coefficients
 * @mul_kernel: %cl_kernel for multiplying Chirp-z coefficients with input
 * @c_mul_kernel: %cl_kernel for complex multiplication
 * @pack_kernel: %cl_kernel for reducing FFT to FT
 * @in_work_size: input 3D size
 * @fft_work_size: FFT 3D size
 * @ft_work_size: FT 3D size
 * @work_n_dims: Number of dimensions for the computations (may be different
 * from the dimensionality of the @param because we may do batching, e.g. have
 * 2D input but perofm many 1D transforms)
 * @crop_width: crop result to this
 * @crop_height: crop result to this
 * @direction: #UfoFftDirection, forward or backward transform
 *
 * Chirp-z transform to get non-power-of-two Fourier Transform. The code is basically doing this (Python):
 *
 *      # L2, N2, M2 are the intermediate sizes for FFT
 *      # L, N, M are depth, height, width of the desired output size
 *
 *      l = fftfreq(L2, 1 / L2)
 *      n = fftfreq(N2, 1 / N2)
 *      m = fftfreq(M2, 1 / M2)
 *      m, n, l = np.meshgrid(l, n, m, indexing='ij')
 *      coeffs = np.exp(-1j * np.pi * (l ** 2 / L + n ** 2 / N + m ** 2 / M))
 *      modulated = arr * coeffs[:L, :N, :M]
 *      dft = coeffs[:L, :N, :M] * ifftn(fftn(modulated, s=(L2, N2, M2)) * fftn(1 / coeffs, s=(L2, N2, M2)))[:L, :N, :M]
 *
 *  The backward direction does the following (equivalent to swapping coeffs for their conjugates):
 *      coeffs = np.exp(-1j * np.pi * (l ** 2 / L + n ** 2 / N + m ** 2 / M))
 *      modulated = arr / coeffs[:L, :N, :M]
 *      idft = 1 / coeffs[:L, :N, :M] * ifftn(fftn(modulated, s=(L2, N2, M2)) * fftn(coeffs, s=(L2, N2, M2)))[:L, :N, :M]
 */
void
ufo_fft_chirp_z (UfoFft *fft,
                 UfoFftParameter *param,
                 cl_command_queue queue,
                 UfoProfiler *profiler,
                 /* Memory */
                 cl_mem in_mem,
                 cl_mem tmp_mem,
                 cl_mem out_mem,
                 UfoBuffer *coeffs_buffer,
                 UfoBuffer *f_coeffs_buffer,
                 /* Kernels */
                 cl_kernel coeffs_kernel,
                 cl_kernel mul_kernel,
                 cl_kernel c_mul_kernel,
                 cl_kernel pack_kernel,
                 /* Sizes */
                 gsize in_work_size[3],
                 gsize fft_work_size[3],
                 gsize ft_work_size[3],
                 gsize work_n_dims,
                 cl_int crop_width,
                 cl_int crop_height,
                 /* FFT or IFFT */
                 UfoFftDirection direction)
{
    UfoRequisition fft_req;
    cl_mem coeffs_mem, f_coeffs_mem;
    cl_int intermediate_width, intermediate_height, plan_dimensions,
           ft_width, ft_height, ft_depth, false_value = 0, true_value = 1;
    gfloat scale = 1.0f;

    plan_dimensions = (cl_int) param->dimensions;
    intermediate_width  = (cl_int) fft_work_size[0];
    intermediate_height = (cl_int) fft_work_size[1];
    fft_req.n_dims = work_n_dims;
    fft_req.dims[0] = fft_work_size[0] << 1;
    fft_req.dims[1] = fft_work_size[1];
    fft_req.dims[2] = fft_work_size[2];

    ft_width  = (cl_int) ft_work_size[0];
    ft_height = (cl_int) ft_work_size[1];
    ft_depth  = (cl_int) ft_work_size[2];

    if (ufo_buffer_cmp_dimensions (coeffs_buffer, &fft_req) != 0) {
        /* First time or size changed, re-create the coefficients */
        ufo_buffer_resize (coeffs_buffer, &fft_req);
        ufo_buffer_resize (f_coeffs_buffer, &fft_req);
        coeffs_mem = ufo_buffer_get_device_array (coeffs_buffer, queue);
        f_coeffs_mem = ufo_buffer_get_device_array (f_coeffs_buffer, queue);

        /* Compute Chirp-z coefficients (coeffs above) */
        if (direction == UFO_FFT_FORWARD) {
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (coeffs_kernel, 0, sizeof (cl_mem), (gpointer) &coeffs_mem));
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (coeffs_kernel, 1, sizeof (cl_mem), (gpointer) &f_coeffs_mem));
        } else {
            /* Swap coeffs and f_coeffs because for IFFT [e^(ix) -> e^(-ix)], i.e. we need conjugates */
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (coeffs_kernel, 0, sizeof (cl_mem), (gpointer) &f_coeffs_mem));
            UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (coeffs_kernel, 1, sizeof (cl_mem), (gpointer) &coeffs_mem));
        }
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (coeffs_kernel, 2, sizeof (cl_int), &ft_width));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (coeffs_kernel, 3, sizeof (cl_int), &ft_height));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (coeffs_kernel, 4, sizeof (cl_int), &ft_depth));
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (coeffs_kernel, 5, sizeof (cl_int), &plan_dimensions));
        ufo_profiler_call (profiler, queue, coeffs_kernel, 3, fft_work_size, NULL);

        UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (fft, queue, profiler,
                                                    f_coeffs_mem, f_coeffs_mem,
                                                    UFO_FFT_FORWARD,
                                                    0, NULL, NULL));
    } else {
        /* Nothing changed, just used the pre-computed values */
        coeffs_mem = ufo_buffer_get_device_array (coeffs_buffer, queue);
        f_coeffs_mem = ufo_buffer_get_device_array (f_coeffs_buffer, queue);
    }

    /* Multiply input with Chirp-z coefficients (modulated = arr * coeffs[:L, :N, :M] above) */
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (mul_kernel, 0, sizeof (cl_mem), (gpointer) &tmp_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (mul_kernel, 1, sizeof (cl_mem), (gpointer) &in_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (mul_kernel, 2, sizeof (cl_mem), (gpointer) &coeffs_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (mul_kernel, 3, sizeof (cl_int), &intermediate_width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (mul_kernel, 4, sizeof (cl_int), &intermediate_height));
    if (direction == UFO_FFT_FORWARD) {
        /* If we are computing FT, input data type is real */
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (mul_kernel, 5, sizeof (cl_int), &false_value));
    } else {
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (mul_kernel, 5, sizeof (cl_int), &true_value));
    }
    ufo_profiler_call (profiler, queue, mul_kernel, 3, in_work_size, NULL);
    /* FFT of modulated */
    UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (fft, queue, profiler,
                                                tmp_mem, tmp_mem,
                                                UFO_FFT_FORWARD,
                                                0, NULL, NULL));

    /* Convolution by multiplication of the FFTs */
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (c_mul_kernel, 0, sizeof (cl_mem), (gpointer) &tmp_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (c_mul_kernel, 1, sizeof (cl_mem), (gpointer) &f_coeffs_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (c_mul_kernel, 2, sizeof (cl_mem), (gpointer) &tmp_mem));
    ufo_profiler_call (profiler, queue, c_mul_kernel, fft_req.n_dims, fft_work_size, NULL);

    /* IFFT */
    UFO_RESOURCES_CHECK_CLERR (ufo_fft_execute (fft, queue, profiler,
                                                tmp_mem, tmp_mem,
                                                UFO_FFT_BACKWARD,
                                                0, NULL, NULL));

    /* Normalization by Chirp-z coefficients */
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (c_mul_kernel, 0, sizeof (cl_mem), (gpointer) &tmp_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (c_mul_kernel, 1, sizeof (cl_mem), (gpointer) &coeffs_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (c_mul_kernel, 2, sizeof (cl_mem), (gpointer) &tmp_mem));
    ufo_profiler_call (profiler, queue, c_mul_kernel, fft_req.n_dims, fft_work_size, NULL);

    /* Scale by the padded FFT size */
    switch (param->dimensions) {
        case UFO_FFT_3D:
            scale /= (gfloat) param->size[2];
        case UFO_FFT_2D:
            scale /= (gfloat) param->size[1];
        case UFO_FFT_1D:
            scale /= (gfloat) param->size[0];
    }

    if (direction == UFO_FFT_BACKWARD) {
        /* Scale by the actual Fourier Transform size */
        switch (param->dimensions) {
            case UFO_FFT_3D:
                scale /= (gfloat) ft_depth;
            case UFO_FFT_2D:
                scale /= (gfloat) ft_height;
            case UFO_FFT_1D:
                scale /= (gfloat) ft_width;
        }
    }
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (pack_kernel, 0, sizeof (cl_mem), (gpointer) &tmp_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (pack_kernel, 1, sizeof (cl_mem), (gpointer) &out_mem));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (pack_kernel, 2, sizeof (cl_int), &crop_width));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (pack_kernel, 3, sizeof (cl_int), &crop_height));
    UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (pack_kernel, 4, sizeof (gfloat), &scale));
    if (direction == UFO_FFT_FORWARD) {
        /* If we are computing FT, resulting data type is complex */
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (pack_kernel, 5, sizeof (cl_int), &true_value));
    } else {
        UFO_RESOURCES_CHECK_CLERR (clSetKernelArg (pack_kernel, 5, sizeof (cl_int), &false_value));
    }
    ufo_profiler_call (profiler, queue, pack_kernel, fft_req.n_dims, fft_work_size, NULL);
}
