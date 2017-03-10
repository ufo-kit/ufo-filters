project(oclfft CXX)

include(GNUInstallDirs)

include_directories(${OpenCL_INCLUDE_DIRS}
                    ${UFO_INCLUDE_DIRS})

add_library(oclfft SHARED
            fft_execute.cpp
            fft_setup.cpp
            fft_kernelstring.cpp)

target_link_libraries(oclfft ${UFO_LIBRARIES} ${OpenCL_LIBRARIES})

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/oclfft.pc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/oclfft.pc" @ONLY IMMEDIATE)

install(TARGETS oclfft
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/oclFFT.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/oclfft.pc
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
