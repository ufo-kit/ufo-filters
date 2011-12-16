cmake_minimum_required(VERSION 2.6)
project(ufo)

set(UFO_VERSION_MAJOR "0")
set(UFO_VERSION_MINOR "1")
set(UFO_VERSION_PATCH "0")
set(UFO_VERSION_STRING_LONG "${UFO_VERSION_MAJOR}.${UFO_VERSION_MINOR}.${UFO_VERSION_PATCH}")
set(UFO_VERSION_STRING_SHORT "${UFO_VERSION_MAJOR}.${UFO_VERSION_MINOR}")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/common/cmake")

# --- Options -----------------------------------------------------------------
option(WITH_PROFILING "Enable profiling" OFF)
if (WITH_PROFILING)
    add_definitions("-pg")
    set(CMAKE_C_FLAGS "-pg")
endif ()

# --- Find packages and libraries ---------------------------------------------
find_package(OpenCL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 glib-2.0>=2.24 REQUIRED)
pkg_check_modules(GOBJECT2 gobject-2.0>=2.24 REQUIRED)
pkg_check_modules(ETHOS ethos-1.0>=0.2.2 REQUIRED)
pkg_check_modules(UFO ufo>=0.1 REQUIRED)

include_directories(
    ${GLIB2_INCLUDE_DIRS}
    ${ETHOS_INCLUDE_DIRS}
    ${OPENCL_INCLUDE_DIRS}
    ${UFO_INCLUDE_DIRS}
    )

add_definitions("-std=c99 -Wall -fPIC")
add_definitions(-DG_LOG_DOMAIN="Ufo")

add_subdirectory(src)

