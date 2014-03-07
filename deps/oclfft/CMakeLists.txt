project(oclfft CXX)
include_directories(${OPENCL_INCLUDE_DIRS}
		    ${UFO_INCLUDE_DIRS})

add_library(oclfft SHARED
            fft_execute.cpp
            fft_setup.cpp
            fft_kernelstring.cpp)

target_link_libraries(oclfft ${OPENCL_LIBRARIES})

install(TARGETS oclfft
        LIBRARY DESTINATION ${UFO_FILTERS_LIBDIR})
