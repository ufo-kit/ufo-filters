include_directories(${OPENCL_INCLUDE_DIRS})

add_library(oclfft STATIC
            fft_execute.cpp
            fft_setup.cpp
            fft_kernelstring.cpp)

set_target_properties(oclfft PROPERTIES LINKER_LANGUAGE C)

target_link_libraries(oclfft ${OPENCL_LIBRARIES})
