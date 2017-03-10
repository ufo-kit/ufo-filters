option(WITH_OCLFFT "Build and distribute Apple FFT" ON)

if (WITH_OCLFFT)
    add_subdirectory(oclfft)
endif ()
