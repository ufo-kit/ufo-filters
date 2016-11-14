project(oclfft CXX)

include_directories(${OpenCL_INCLUDE_DIRS}
                    ${UFO_INCLUDE_DIRS})

add_library(oclfft SHARED
            fft_execute.cpp
            fft_setup.cpp
            fft_kernelstring.cpp)

target_link_libraries(oclfft ${UFO_LIBRARIES} ${OpenCL_LIBRARIES})

install(TARGETS oclfft
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
