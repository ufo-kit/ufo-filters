option(WITH_OCLFFT "Use bundled OpenCL FFT backend" ON)

if (WITH_OCLFFT)
    add_subdirectory(oclfft)
endif ()
