kernel void frac(global float *a, global float *b, global float *c){
    size_t idx = get_global_id (1) * get_global_size (0) + get_global_id (0);
    c[idx] = a[idx] / b[idx];
}


// a complex input
kernel void bandpass(global float *a, global float *output, const float f_0, const float f_1, const float sigma_0, const float sigma_1, const int keep_zero_frequency ){
    //const bool keep_zero_frequency = true;
    //const float f_0 = 0.05;
    //const float f_1 = 0.6;
    //const float sigma_0 = 0.01;
    //const float sigma_1 = 0.01;


    const int width = get_global_size(0);
    const int height = get_global_size(1);
    int idx = get_global_id(0);
    int idy = get_global_id(1);
    int index = idy * width * 2 + idx * 2;


    
    float px = ((idx >= width >> 1) ? idx - width : idx);
    float py = ((idy >= height >> 1) ? idy - height : idy);
    px = px / width;
    py = py / height;

    float r = sqrt(px*px+py*py);

    float filter = 0.5 * (1 + erf( (r - f_0) / (sigma_0 * sqrt(2.)))) - 0.5 * (1 + erf((r - f_1) / (sigma_1 * sqrt(2.))));

    if(keep_zero_frequency && (index == 0 || index == 1)){
        output[index] = a[index];
        output[index+1] = a[index+1];

    }
    else{
        output[index] = a[index] * filter;
        output[index+1] = a[index+1] * filter;
    }
}