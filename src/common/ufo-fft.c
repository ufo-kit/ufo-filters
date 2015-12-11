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


UfoFft *
ufo_fft_new (void)
{
    UfoFft *fft;

    fft = g_malloc0 (sizeof (UfoFft));

#ifdef HAVE_AMD
    UFO_RESOURCES_CHECK_CLERR (clfftSetup (&fft->amd_setup));
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
    clfftDestroyPlan (&fft->amd_plan);
    clfftTeardown ();
#else
    clFFT_DestroyPlan (fft->apple_plan);
#endif

    g_free (fft);
}
