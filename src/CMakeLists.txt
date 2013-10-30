cmake_minimum_required(VERSION 2.6)

#{{{ Sources
set(ufofilter_SRCS 
    ufo-averager-task.c
    ufo-backproject-task.c
    ufo-buffer-task.c
    ufo-cut-sinogram-task.c
    ufo-center-of-rotation-task.c
    ufo-dfi-sinc-task.c
    ufo-filter-task.c
    ufo-flat-field-correction-task.c
    ufo-gaussian-blur-task.c
    ufo-sharpness-measure-task.c
    ufo-meta-balls-task.c
    ufo-null-task.c
    ufo-opencl-task.c
    ufo-region-of-interest-task.c
    ufo-sino-generator-task.c
    ufo-swap-quadrants-task.c
    ufo-subtract-task.c
    ufo-volume-render-task.c
    ufo-zeropadding-task.c
    )
#}}}
#{{{ Kernels
set(ufofilter_KERNELS
    arithmetics.cl
    backproject.cl
    complex.cl
    cut_sinogram_kernel.cl
    dfi_sinc_kernel.cl
    fft.cl
    filter.cl
    forwardproject.cl
    gaussian.cl
    histthreshold.cl
    interpolator.cl
    metaballs.cl
    nlm.cl
    swap_quadrants_kernel.cl
    volume.cl
    zeropadding_kernel.cl
    )
#}}}
#{{{ Variables
set(ufofilter_LIBS ${UFO_LIBRARIES})
#}}}
#{{{ Dependency checks
find_package(TIFF)
pkg_check_modules(UCA libuca>=1.2)
pkg_check_modules(OPENCV opencv)
pkg_check_modules(UFOART ufoart>=0.1)
pkg_check_modules(OCLFFT oclfft)

if (TIFF_FOUND)
    list(APPEND ufofilter_SRCS ufo-reader-task.c)
    list(APPEND ufofilter_SRCS ufo-writer-task.c)
    list(APPEND ufofilter_LIBS ${TIFF_LIBRARIES})
    include_directories(${TIFF_INCLUDE_DIRS})
endif ()

if (UCA_INCLUDE_DIRS AND UCA_LIBRARIES)
    list(APPEND ufofilter_SRCS ufo-camera-task.c)
    list(APPEND ufofilter_LIBS ${UCA_LIBRARIES})
    include_directories(${UCA_INCLUDE_DIRS})
endif ()

if (OCLFFT_FOUND)
    set(HAVE_OCLFFT "1")
    set(ufofilter_LIBS ${ufofilter_LIBS} ${OCLFFT_LIBRARIES})
    include_directories(${OCLFFT_INCLUDE_DIRS})
endif ()
#
#if (FFTW3_FOUND)
#    set(HAVE_FFTW3 "1")
#    set(ufofilter_LIBS ${ufofilter_LIBS} ${FFTW3_LIBRARIES})
#    include_directories(${FFTW3_INCLUDE_DIRS})
#endif ()
#
if (OCLFFT_FOUND OR FFTW3_FOUND)
    list(APPEND ufofilter_SRCS ufo-fft-task.c)
    list(APPEND ufofilter_SRCS ufo-ifft-task.c)
endif ()
#
#if (OPENCV_FOUND)
#    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-cv-show.c)
#    set(ufofilter_LIBS ${ufofilter_LIBS} ${OPENCV_LIBRARIES})
#    include_directories(${OPENCV_INCLUDE_DIRS})
#endif ()

if (UFOART_FOUND)
    include_directories(${UFOART_INCLUDE_DIRS})
    set(ufofilter_LIBS ${ufofilter_LIBS} ${UFOART_LIBRARIES})
    list(APPEND ufofilter_SRCS ufo-art-task.c)
endif()
#}}}
#{{{ Plugin targets
get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)

set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}/ufo")
set(shared_objects "")
set(documented_types "")
set(all_targets)

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
        set(documented_types "${documented_types}\nufo_${_misc}_task_get_type")
        set(_misc "${_misc}_misc_SRCS")

        string(REPLACE "-" "" _targetname ${task})
        set(target "ufofilter${_targetname}")
        set(shared_objects "${shared_objects} -l${target}")

        # build single shared library per filter
        if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            add_library(${target} MODULE ${_src} ${${_misc}})
        else()
            add_library(${target} SHARED ${_src} ${${_misc}})
        endif()

        target_link_libraries(${target} ${ufofilter_LIBS})
        list(APPEND all_targets ${target})
        
        install(TARGETS ${target}
            ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
            LIBRARY DESTINATION ${LIB_INSTALL_DIR})
    endif()
endforeach()

# copy kernels into $LIB_INSTALL_DIR
foreach(_kernel ${ufofilter_KERNELS})
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${_kernel}
                   ${CMAKE_CURRENT_BINARY_DIR}/${_kernel})

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${_kernel}
            DESTINATION ${LIB_INSTALL_DIR})
endforeach()
#}}}
#{{{ gtk-doc targets
pkg_check_modules(GTK_DOC gtk-doc)

if(GTK_DOC_FOUND)
    option(WITH_GTK_DOC "Build filter documentation" ON)
    if (WITH_GTK_DOC)
        get_directory_property(_current_include_dirs INCLUDE_DIRECTORIES)

        set(GTK_DOC_CFLAGS)
        foreach(_incl ${_current_include_dirs})
            set(GTK_DOC_CFLAGS "-I${_incl} ${GTK_DOC_CFLAGS}")
        endforeach()

        set(GTK_DOC_LDFLAGS)
        foreach(_lib ${ufofilter_LIBS})
            # check if we have an absolute library path
            if (NOT ${_lib} MATCHES "^[/]")
                set(GTK_DOC_LDFLAGS "-l${_lib} ${GTK_DOC_LDFLAGS}")
            endif()
        endforeach()

        find_program(GTK_DOC_SCAN gtkdoc-scan REQUIRED)
        find_program(GTK_DOC_SCANGOBJ gtkdoc-scangobj REQUIRED)
        find_program(GTK_DOC_MKDB gtkdoc-mkdb REQUIRED)
        find_program(GTK_DOC_MKHTML gtkdoc-mkhtml REQUIRED)

        set(doc_base "${CMAKE_CURRENT_BINARY_DIR}/../docs")
        set(doc_out "${doc_base}/reference")

        set(_xml_doc_input)

        foreach (_src ${ufofilter_SRCS})
            string(REPLACE ".c" ".xml" _xml_doc ${_src})
            list(APPEND _xml_doc_input "<xi:include href=\"xml/${_xml_doc}\"/>")
        endforeach()

        string(REPLACE ";" "\n" _xml_doc_input ${_xml_doc_input})
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/UfoFilters-docs.xml.in" "${doc_out}/UfoFilters-docs.xml")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/scangobj.sh.in" "${doc_out}/scangobj.sh")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/UfoFilters.types.in" "${doc_out}/UfoFilters.types")

        add_custom_command(OUTPUT ${doc_out}/UfoFilters-decl.txt
            COMMAND ${GTK_DOC_SCAN}
                    --module=UfoFilters
                    --source-dir=${CMAKE_CURRENT_SOURCE_DIR}/../src
                    DEPENDS ${ufofilter_SRCS}
            WORKING_DIRECTORY ${doc_out})

        add_custom_command(OUTPUT ${doc_out}/UfoFilters.args
            COMMAND sh scangobj.sh
            DEPENDS ${ufofilter_SRCS} ${doc_out}/UfoFilters-decl.txt ${all_targets}
            WORKING_DIRECTORY ${doc_out})

        add_custom_command(OUTPUT ${doc_out}/sgml.stamp
            COMMAND ${GTK_DOC_MKDB}
                    --module=UfoFilters
                    --source-dir=${CMAKE_CURRENT_SOURCE_DIR}
                    --output-format=xml
            DEPENDS ${ufofilter_SRCS} ${doc_out}/UfoFilters.args
            WORKING_DIRECTORY ${doc_out})

        add_custom_command(OUTPUT ${doc_base}/html.stamp
            COMMAND ${GTK_DOC_MKHTML}
                    UfoFilters
                    ${doc_out}/UfoFilters-docs.xml
            DEPENDS ${doc_out}/sgml.stamp
            WORKING_DIRECTORY ${doc_out})

        add_custom_target(reference ALL DEPENDS ${doc_base}/html.stamp)
    endif()
endif(GTK_DOC_FOUND)
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
