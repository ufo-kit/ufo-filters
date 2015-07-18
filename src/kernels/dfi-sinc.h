#ifndef __UFO_DFI_SINC_TASK_H
#define __UFO_DFI_SINC_TASK_H

typedef struct {
        float real;
        float imag;
} clFFT_Complex;

typedef struct {
    int   half_ktbl_length;
    int   raster_size;
    int   spectrum_offset;

    float half_kernel_length; // L2
    float table_spacing;
    float inv_angle_step_rad;
    float theta_max;
    float rho_max;
    float radius_max;
} DfiSincData;

#endif