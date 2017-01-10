##
## This file is part of ufo-serge filter set.
## Copyright (C) 2016 Serge Cohen
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
##
## Serge Cohen <serge.cohen@synchrotron-soleil.fr>
##

set(contribs_sxc_filter_SRCS
   ufo-med-mad-reject-2d-task.c
   ufo-med-mad-reject-task.c
   ufo-ocl-1liner-task.c
   ufo-stat-monitor-task.c
   )

set(contribs_sxc_aux_SRCS
    ufo-sxc-common.c)

file(GLOB contribs_sxc_filter_KERNELS "kernels/*.cl")

include(ConfigurePaths)
include(PkgConfigVars)
include(CheckCSourceCompiles)

configure_paths(CONTRIBS_SXC_FILTERS)

find_package(OpenCL REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(UFO ufo>=${PKG_UFO_CORE_MIN_REQUIRED} REQUIRED)
pkg_check_variable(ufo plugindir)
pkg_check_variable(ufo kerneldir)

add_definitions("-std=c99 -Wall -fPIC")
add_definitions(-DG_LOG_DOMAIN="Ufo")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}
                    ${OPENCL_INCLUDE_DIRS}
                    ${UFO_INCLUDE_DIRS})

link_directories(${UFO_LIBRARY_DIRS} ${OPENCL_LIBRARY_DIRS})

set(CMAKE_REQUIRED_INCLUDES ${OPENCL_INCLUDE_DIRS})

set(ufofilter_LIBS
    m
    ${UFO_LIBRARIES}
    ${OpenCL_LIBRARIES})

# build static auxiliary library first
add_library(contribs_sxc_aux STATIC ${contribs_sxc_aux_SRCS})

foreach(_src ${contribs_sxc_filter_SRCS})
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
        add_library(${target} MODULE ${_src} ${${_misc}})
    else()
        add_library(${target} SHARED ${_src} ${${_misc}})
    endif()

    target_link_libraries(${target} ${ufofilter_LIBS} ${${_aux_libs}} contribs_sxc_aux)

    list(APPEND all_targets ${target})

    install(TARGETS ${target}
            ARCHIVE DESTINATION ${UFO_PLUGINDIR}
            LIBRARY DESTINATION ${UFO_PLUGINDIR})
endforeach()


# copy kernels
foreach(_kernel ${contribs_sxc_filter_KERNELS})
    install(FILES ${_kernel} DESTINATION ${UFO_KERNELDIR})
endforeach()
