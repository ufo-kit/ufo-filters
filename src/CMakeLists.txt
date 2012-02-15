cmake_minimum_required(VERSION 2.6)

# --- Set sources -------------------------------------------------------------
set(ufofilter_SRCS 
    ufo-filter-arg-max.c
    ufo-filter-averager.c
    ufo-filter-backproject.c
    ufo-filter-forward-project.c
    ufo-filter-center-of-rotation.c
    ufo-filter-cl.c
    ufo-filter-complex.c
    ufo-filter-copy.c
    ufo-filter-demux.c
    ufo-filter-filter.c
    ufo-filter-interpolator.c
    ufo-filter-meta-balls.c
    ufo-filter-mux.c
    ufo-filter-normalize.c
    ufo-filter-null.c
    ufo-filter-pipe-output.c
    ufo-filter-sino-generator.c
    ufo-filter-optical-flow-lucas-kanade.c
    )

set(ufofilter_KERNELS
    backproject.cl
    forwardproject.cl
    fft.cl
    filter.cl
    interpolator.cl
    complex.cl
    filters.cl
    lkflow.cl
    metaballs.cl
    motion.cl
    nlm.cl
    )

set(ufofilter_LIBS
    ${GLIB2_LIBRARIES}
    ${GOBJECT2_LIBRARIES}
    )


# --- Add filters that depend on other libraries ------------------------------
find_package(TIFF)
find_package(OCLFFT)
pkg_check_modules(UCA uca)
pkg_check_modules(OPENCV opencv)

if (UCA_INCLUDE_DIRS AND UCA_LIBRARIES)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-uca.c)
    set(ufofilter_LIBS ${ufofilter_LIBS} ${UCA_LIBRARIES})
    include_directories(${UCA_INCLUDE_DIRS})
endif ()

if (TIFF_FOUND)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-reader.c)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-writer.c)
    set(ufofilter_LIBS ${ufofilter_LIBS} ${TIFF_LIBRARIES})
    include_directories(${TIFF_INCLUDE_DIRS})
endif ()

if (OCLFFT_FOUND)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-fft.c)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-ifft.c)
    set(ufofilter_LIBS ${ufofilter_LIBS} ${OCLFFT_LIBRARIES})
    include_directories(${OCLFFT_INCLUDE_DIRS})
endif ()

if (OPENCV_FOUND)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-cv-show.c)
    set(ufofilter_LIBS ${ufofilter_LIBS} ${OPENCV_LIBRARIES})
    include_directories(${OPENCV_INCLUDE_DIRS})
endif ()


# --- Add sources that belong to other libraries ------------------------------
set(optical_flow_lucas_kanade_misc_SRCS
    oflk_cl_buffer.c
    oflk_cl_image.c
    oflk_pyramid.c)


# --- Target ------------------------------------------------------------------
include_directories(${CMAKE_CURRENT_BINARY_DIR})

get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)

set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}/ufo")

foreach(_src ${ufofilter_SRCS})
    # find plugin suffix
    string(REGEX REPLACE "ufo-filter-([^ \\.]+).*" "\\1" filter "${_src}")

    # build string to get miscalleanous sources
    string(REPLACE "-" "_" _misc ${filter})
    set(_misc "${_misc}_misc_SRCS")

    string(REPLACE "-" "" _targetname ${filter})
    set(target "${_targetname}")

    # build single shared library per filter
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        add_library(${target} MODULE ${_src} ${${_misc}})
    else()
        add_library(${target} SHARED ${_src} ${${_misc}})
    endif()

    set_target_properties(${target} PROPERTIES PREFIX "")
    target_link_libraries(${target} ${ufofilter_LIBS})
    
    install(TARGETS ${target}
        ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
        LIBRARY DESTINATION ${LIB_INSTALL_DIR})
endforeach()

# copy kernels into $LIB_INSTALL_DIR
foreach(_kernel ${ufofilter_KERNELS})
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${_kernel}
        ${CMAKE_CURRENT_BINARY_DIR}/${_kernel})

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${_kernel}
        DESTINATION ${LIB_INSTALL_DIR})
endforeach()
