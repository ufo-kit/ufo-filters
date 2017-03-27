cmake_minimum_required(VERSION 2.8)

if (POLICY CMP0046)
cmake_policy(SET CMP0046 OLD)
endif ()

if (POLICY CMP0053)
cmake_policy(SET CMP0053 OLD)
endif ()

if (POLICY CMP0054)
cmake_policy(SET CMP0054 OLD)
endif ()

project(ufo C)

set(TARNAME "ufo-filters")

set(UFO_FILTERS_VERSION_MAJOR "0")
set(UFO_FILTERS_VERSION_MINOR "13")
set(UFO_FILTERS_VERSION_PATCH "0")
set(UFO_FILTERS_VERSION_STRING_LONG "${UFO_FILTERS_VERSION_MAJOR}.${UFO_FILTERS_VERSION_MINOR}.${UFO_FILTERS_VERSION_PATCH}")
set(UFO_FILTERS_VERSION_STRING_SHORT "${UFO_FILTERS_VERSION_MAJOR}.${UFO_FILTERS_VERSION_MINOR}")

set(UFO_DESCRIPTION "UFO good filters")
set(UFO_DESCRIPTION_SUMMARY "UFO good filters")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/common/cmake")

if (APPLE)
    set(CMAKE_MACOSX_RPATH      "ON")
    set(CMAKE_INSTALL_RPATH     "${CMAKE_INSTALL_PREFIX}/lib")
endif()

#{{{ Variables and Options
include(GNUInstallDirs)
include(PkgConfigVars)

set(PKG_UFO_CORE_MIN_REQUIRED "0.12")
# Backprojection burst mode, must be one of 1, 2, 4, 8, 16
set(BP_BURST "16" CACHE STRING "Number of projections processed in one pass")

option(WITH_PROFILING "Enable profiling" OFF)

if (WITH_PROFILING)
    add_definitions("-pg")
    set(CMAKE_C_FLAGS "-pg")
endif ()

option(WITH_CONTRIB "Enable contrib filters" OFF)
#}}}
#{{{ Common dependencies
find_package(OpenCL REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(UFO ufo>=${PKG_UFO_CORE_MIN_REQUIRED} REQUIRED)

pkg_check_variable(ufo plugindir)
pkg_check_variable(ufo kerneldir)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/oclfft)

link_directories(${UFO_LIBRARY_DIRS})
#}}}
#{{{ Global definitions
add_definitions("-Wall -Wextra -fPIC")
add_definitions(-DG_LOG_DOMAIN="Ufo")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")

if (CMAKE_COMPILER_IS_GNUCC OR ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang"))
    add_definitions("-Wno-unused-parameter")
endif ()
#}}}
#{{{ Subdirectories
add_subdirectory(docs)
add_subdirectory(deps)
add_subdirectory(src)
add_subdirectory(utils)
if (WITH_CONTRIB)
    add_subdirectory(contrib)
endif ()
#}}}
