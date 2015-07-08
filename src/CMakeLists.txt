cmake_minimum_required(VERSION 2.6)

#{{{ Sources
set(ufofilter_SRCS
    ufo-average-task.c
    ufo-backproject-task.c
    ufo-blur-task.c
    ufo-buffer-task.c
    ufo-calculate-task.c
    ufo-crop-task.c
    ufo-cut-sinogram-task.c
    ufo-center-of-rotation-task.c
    ufo-concatenate-result-task.c
    ufo-contrast-task.c
    ufo-dfi-sinc-task.c
    ufo-denoise-task.c
    ufo-detect-edge-task.c
    ufo-rescale-task.c
    ufo-dummy-data-task.c
    ufo-dump-ring-task.c
    ufo-duplicate-task.c
    ufo-filter-task.c
    ufo-flatten-task.c
    ufo-flatten-inplace-task.c
    ufo-flat-field-correct-task.c
    ufo-fft-task.c
    ufo-fftmult-task.c
    ufo-filter-particle-task.c
    ufo-filter-stripes-task.c
    ufo-forwardproject-task.c
    ufo-get-dup-circ-task.c
    ufo-ifft-task.c
    ufo-interpolate-task.c
    ufo-map-slice-task.c
    ufo-measure-sharpness-task.c
    ufo-median-filter-task.c
    ufo-metaballs-task.c
    ufo-monitor-task.c
    ufo-multi-search-task.c
    ufo-null-task.c
    ufo-opencl-task.c
    ufo-ordfilt-task.c
    ufo-pad-task.c
    ufo-polar-coordinates-task.c
    ufo-read-task.c
    ufo-reduce-task.c
    ufo-remove-circle-task.c
    ufo-retrieve-phase-task.c
    ufo-ring-pattern-task.c
    ufo-ringwriter-task.c
    ufo-replicate-task.c
    ufo-slice-task.c
    ufo-stack-task.c
    ufo-transpose-task.c
    ufo-transpose-projections-task.c
    ufo-swap-quadrants-task.c
    ufo-subtract-task.c
    ufo-volume-render-task.c
    ufo-write-task.c
    ufo-zeropad-task.c
    )

set(ufoaux_SRCS
    ufo-priv.c)

set(read_misc_SRCS
    readers/ufo-reader.c
    readers/ufo-edf-reader.c)

set(write_misc_SRCS
    writers/ufo-writer.c
    writers/ufo-raw-writer.c)

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
find_package(HDF5 1.8)
find_package(JPEG)

pkg_check_modules(UCA libuca>=1.2)
pkg_check_modules(OPENCV opencv)
pkg_check_modules(LIBTIFF4 libtiff-4>=4.0.0)
pkg_check_modules(GSL gsl)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fopenmp")

if (TIFF_FOUND)
    list(APPEND read_misc_SRCS readers/ufo-tiff-reader.c)
    list(APPEND write_misc_SRCS writers/ufo-tiff-writer.c)
    list(APPEND ufofilter_LIBS ${TIFF_LIBRARIES})
    include_directories(${TIFF_INCLUDE_DIRS})
    link_directories(${TIFF_LIBRARY_DIRS})
    set(HAVE_TIFF True)
elseif (LIBTIFF4_INCLUDE_DIRS AND LIBTIFF4_LIBRARIES)
    list(APPEND read_misc_SRCS readers/ufo-tiff-reader.c)
    list(APPEND write_misc_SRCS writers/ufo-tiff-writer.c)
    list(APPEND ufofilter_LIBS ${LIBTIFF4_LIBRARIES})
    include_directories(${LIBTIFF4_INCLUDE_DIRS})
    link_directories(${LIBTIFF4_LIBRARY_DIRS})
    set(HAVE_TIFF True)
endif ()

if (JPEG_FOUND)
    list(APPEND write_misc_SRCS writers/ufo-jpeg-writer.c)
    list(APPEND ufofilter_LIBS ${JPEG_LIBRARIES})
    include_directories(${JPEG_INCLUDE_DIRS})
    link_directories(${JPEG_LIBRARY_DIRS})
    set(HAVE_JPEG True)
endif ()

if (HDF5_FOUND)
    option(WITH_HDF5 "Use HDF5" ON)
    if (WITH_HDF5)
        # This hack is proudly presented by stupid CMake configure scripts and a
        # badly written libhdf5.
        find_path(MPI_INCLUDE_PATH NAMES mpi.h PATHS /usr/include /usr/include/mpich2 /usr/lib/openmpi/include)

        if (MPI_INCLUDE_PATH)
            include_directories(${MPI_INCLUDE_PATH})
        endif ()

        list(APPEND read_misc_SRCS readers/ufo-hdf5-reader.c common/hdf5.c)
        list(APPEND write_misc_SRCS writers/ufo-hdf5-writer.c common/hdf5.c)
        list(APPEND ufofilter_LIBS ${HDF5_LIBRARIES})
        include_directories(${HDF5_INCLUDE_DIRS})
        link_directories(${HDF5_LIBRARY_DIRS})
    endif ()
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
    set(_misc "${_misc}_misc_SRCS")

    string(REPLACE "-" "" _targetname ${task})
    set(target "ufofilter${_targetname}")

    # build single shared library per filter
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        add_library(${target} MODULE ${ufoaux_SRCS} ${_src} ${${_misc}})
    else()
        add_library(${target} SHARED ${ufoaux_SRCS} ${_src} ${${_misc}})
    endif()

    target_link_libraries(${target} ${ufofilter_LIBS})

    list(APPEND all_targets ${target})

    install(TARGETS ${target}
            ARCHIVE DESTINATION ${UFO_PLUGINDIR}
            LIBRARY DESTINATION ${UFO_PLUGINDIR})
endforeach()

# copy kernels
foreach(_kernel ${ufofilter_KERNELS})
    install(FILES ${_kernel} DESTINATION ${UFO_KERNELDIR})
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
