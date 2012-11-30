cmake_minimum_required(VERSION 2.6)

# --- Set sources -------------------------------------------------------------
set(ufofilter_SRCS 
    ufo-filter-averager.c
    ufo-filter-backproject.c
    ufo-filter-buffer-input.c
    ufo-filter-forward-project.c
    ufo-filter-center-of-rotation.c
    ufo-filter-cl.c
    ufo-filter-complex.c
    ufo-filter-filter.c
    ufo-filter-histogram-threshold.c
    ufo-filter-gaussian-blur.c
    ufo-filter-interpolator.c
    ufo-filter-meta-balls.c
    ufo-filter-normalize.c
    ufo-filter-null.c
    ufo-filter-pipe-output.c
    ufo-filter-region-of-interest.c
    ufo-filter-sino-generator.c
    ufo-filter-subtract.c
    ufo-filter-volume-renderer.c
    )

set(ufofilter_KERNELS
    backproject.cl
    forwardproject.cl
    fft.cl
    filter.cl
    gaussian.cl
    histthreshold.cl
    interpolator.cl
    complex.cl
    filters.cl
    metaballs.cl
    motion.cl
    nlm.cl
    volume.cl
    stdp.cl
    )

set(ufofilter_LIBS
    ${UFO_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${GOBJECT2_LIBRARIES}
    )

link_directories(${UFO_LIBRARY_DIRS})

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# --- Add filters that depend on other libraries ------------------------------
find_package(TIFF)
find_package(OCLFFT)
find_program(RE2C re2c)
pkg_check_modules(UCA uca>=1.0)
pkg_check_modules(OPENCV opencv)
pkg_check_modules(FFTW3 fftw3f)

if (UCA_INCLUDE_DIRS AND UCA_LIBRARIES)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-cam-access.c)
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
    set(HAVE_OCLFFT "1")
    set(ufofilter_LIBS ${ufofilter_LIBS} ${OCLFFT_LIBRARIES})
    include_directories(${OCLFFT_INCLUDE_DIRS})
endif ()

if (FFTW3_FOUND)
    set(HAVE_FFTW3 "1")
    set(ufofilter_LIBS ${ufofilter_LIBS} ${FFTW3_LIBRARIES})
    include_directories(${FFTW3_INCLUDE_DIRS})
endif ()

if (OCLFFT_FOUND OR FFTW3_FOUND)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-fft.c)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-ifft.c)
endif ()

if (OPENCV_FOUND)
    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-cv-show.c)
    set(ufofilter_LIBS ${ufofilter_LIBS} ${OPENCV_LIBRARIES})
    include_directories(${OPENCV_INCLUDE_DIRS})
endif ()

if (RE2C)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/expr-scanner.c
        COMMAND ${RE2C} 
                -o ${CMAKE_CURRENT_BINARY_DIR}/expr-scanner.c
                ${CMAKE_CURRENT_SOURCE_DIR}/expr-scanner.c.in
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/expr-scanner.c.in)

    set(ufofilter_SRCS ${ufofilter_SRCS} ufo-filter-expr.c)
    set(expr_misc_SRCS
        expr-parser.c
        ${CMAKE_CURRENT_BINARY_DIR}/expr-scanner.c)
endif ()


# --- Add sources that belong to other libraries ------------------------------
set(optical_flow_lucas_kanade_misc_SRCS
    oflk_cl_buffer.c
    oflk_cl_image.c
    oflk_pyramid.c)


# --- Target ------------------------------------------------------------------
get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)

set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}/ufo")
set(shared_objects "")
set(documented_types "")
set(all_targets)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

foreach(_src ${ufofilter_SRCS})
    # find plugin suffix
    string(REGEX REPLACE "ufo-filter-([^ \\.]+).*" "\\1" filter "${_src}")

    # build string to get miscalleanous sources
    string(REPLACE "-" "_" _misc ${filter})
    string(TOUPPER ${_misc} _misc_upper)

    # create an option name and add this to disable filters
    set(target_option "ENABLE_${_misc_upper}")
    option(${target_option} "Build filter ${filter}" ON)

    if (${target_option})
        set(documented_types "${documented_types}\nufo_filter_${_misc}_get_type")
        set(_misc "${_misc}_misc_SRCS")

        string(REPLACE "-" "" _targetname ${filter})
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


# --- Build filter reference ---------------------------------------------------
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
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/UfoFilters-sections.txt.in" "${doc_out}/UfoFilters-sections.txt")
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


# --- Execute nosetest --------------------------------------------------------
find_program(NOSETESTSBIN nosetests)
if (NOSETESTSBIN)
    add_custom_target(test 
        COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/../run_tests.py
        ${CMAKE_CURRENT_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../)
    add_dependencies(test ${all_targets})
endif()

