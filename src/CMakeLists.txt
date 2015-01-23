cmake_minimum_required(VERSION 2.6)

#{{{ Sources
set(ufofilter_SRCS
    ufo-averager-task.c
    ufo-backproject-task.c
    ufo-buffer-task.c
    ufo-cut-sinogram-task.c
    ufo-center-of-rotation-task.c
    ufo-dfi-sinc-task.c
    ufo-downsample-task.c
    ufo-filter-task.c
    ufo-flatten-task.c
    ufo-flatten-inplace-task.c
    ufo-flat-field-correction-task.c
    ufo-fft-task.c
    ufo-forwardproject-task.c
    ufo-gaussian-blur-task.c
    ufo-generate-task.c
    ufo-ifft-task.c
    ufo-interpolate-task.c
    ufo-sharpness-measure-task.c
    ufo-median-filter-task.c
    ufo-meta-balls-task.c
    ufo-null-task.c
    ufo-opencl-task.c
    ufo-phase-retrieval-task.c
    ufo-polar-coordinates-task.c
    ufo-region-of-interest-task.c
    ufo-sino-generator-task.c
    ufo-slice-task.c
    ufo-stack-task.c
    ufo-stripe-filter-task.c
    ufo-transpose-task.c
    ufo-swap-quadrants-task.c
    ufo-subtract-task.c
    ufo-volume-render-task.c
    ufo-zeropadding-task.c
    ufo-scalability-task.c
    )

file(GLOB ufofilter_KERNELS "kernels/*.cl")
#}}}
#{{{ Variables
set(ufofilter_LIBS
    ${UFO_LIBRARIES}
    oclfft)

if (CMAKE_COMPILER_IS_GNUCC OR ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang"))
    add_definitions("-Wcast-align -Wcast-qual -Winline -Wmissing-declarations "
                    "-Wmissing-prototypes -Wnested-externs -Wno-long-long "
                    "-Wno-missing-field-initializers -Wpointer-arith "
                    "-Wredundant-decls -Wshadow -Wstrict-prototypes -Wwrite-strings")
endif()
#}}}
#{{{ Dependency checks
find_package(TIFF)
find_package(clFFT)

pkg_check_modules(UCA libuca>=1.2)
pkg_check_modules(OPENCV opencv)
pkg_check_modules(LIBTIFF4 libtiff-4>=4.0.0)
pkg_check_modules(GSL gsl)
pkg_check_modules(UFO_IR ufoir>=0.1)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fopenmp")

if (TIFF_FOUND)
    list(APPEND ufofilter_SRCS ufo-reader-task.c)
    list(APPEND ufofilter_SRCS ufo-writer-task.c)
    list(APPEND ufofilter_LIBS ${TIFF_LIBRARIES})
    include_directories(${TIFF_INCLUDE_DIRS})
    link_directories(${TIFF_LIBRARY_DIRS})
elseif (LIBTIFF4_INCLUDE_DIRS AND LIBTIFF4_LIBRARIES)
    list(APPEND ufofilter_SRCS ufo-reader-task.c)
    list(APPEND ufofilter_SRCS ufo-writer-task.c)
    list(APPEND ufofilter_LIBS ${LIBTIFF4_LIBRARIES})
    include_directories(${LIBTIFF4_INCLUDE_DIRS})
    link_directories(${LIBTIFF4_LIBRARY_DIRS})
endif ()

if (UCA_INCLUDE_DIRS AND UCA_LIBRARIES)
    list(APPEND ufofilter_SRCS ufo-camera-task.c)
    list(APPEND ufofilter_LIBS ${UCA_LIBRARIES})
    include_directories(${UCA_INCLUDE_DIRS})
    link_directories(${UCA_LIBRARY_DIRS})
endif ()

if (GSL_FOUND)
    include_directories(${GSL_INCLUDE_DIRS})
    list(APPEND ufofilter_LIBS ${GSL_LIBRARIES})
    list(APPEND ufofilter_SRCS ufo-measure-task.c)
endif ()

if (UFO_IR_FOUND)
    include_directories(${UFO_IR_INCLUDE_DIRS})
    set(ufofilter_LIBS ${ufofilter_LIBS} ${UFO_IR_LIBRARIES})
    list(APPEND ufofilter_SRCS ufo-ir-task.c)
endif()

if (CLFFT_FOUND)
    include_directories(${CLFFT_INCLUDE_DIRS})
    list(APPEND ufofilter_LIBS ${CLFFT_LIBRARIES})
    set(HAVE_AMD ON)
endif ()

#}}}
#{{{ Plugin targets
include_directories(${CMAKE_CURRENT_BINARY_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}
                    ${OPENCL_INCLUDE_DIRS}
                    ${UFO_INCLUDE_DIRS})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/config.h)

foreach(_src ${ufofilter_SRCS})
    # find plugin suffix
    string(REGEX REPLACE "ufo-([^ \\.]+)-task.*" "\\1" task "${_src}")

    # build string to get miscalleanous sources
    string(REPLACE "-" "_" _misc ${task})
    string(TOUPPER ${_misc} _misc_upper)

    # create an option name and add this to disable filters
    set(target_option "ENABLE_${_misc_upper}")
    option(${target_option} "Build filter ${task}" ON)

    if (${target_option})
        set(_misc "${_misc}_misc_SRCS")

        string(REPLACE "-" "" _targetname ${task})
        set(target "ufofilter${_targetname}")

        # build single shared library per filter
        if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            add_library(${target} MODULE ${_src} ${${_misc}})
        else()
            add_library(${target} SHARED ${_src} ${${_misc}})
        endif()

        target_link_libraries(${target} ${ufofilter_LIBS})

        list(APPEND all_targets ${target})

        install(TARGETS ${target}
                ARCHIVE DESTINATION ${UFO_FILTERS_PLUGINDIR}
                LIBRARY DESTINATION ${UFO_FILTERS_PLUGINDIR})
    endif()
endforeach()

# copy kernels
foreach(_kernel ${ufofilter_KERNELS})
    install(FILES ${_kernel} DESTINATION ${UFO_FILTERS_KERNELDIR})
endforeach()
#}}}
#{{{ Python tests
find_program(NOSETESTSBIN nosetests)
if (NOSETESTSBIN)
    add_custom_target(test
        COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/../run_tests.py
        ${CMAKE_CURRENT_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../)
    add_dependencies(test ${all_targets})
endif()
#}}}
