cmake_minimum_required(VERSION 2.6)
project(ufo C)

set(TARNAME "ufo-filters")
set(UFO_FILTERS_VERSION_MAJOR "0")
set(UFO_FILTERS_VERSION_MINOR "2")
set(UFO_FILTERS_VERSION_PATCH "0")
set(UFO_FILTERS_VERSION_STRING_LONG "${UFO_FILTERS_VERSION_MAJOR}.${UFO_FILTERS_VERSION_MINOR}.${UFO_FILTERS_VERSION_PATCH}")
set(UFO_FILTERS_VERSION_STRING_SHORT "${UFO_FILTERS_VERSION_MAJOR}.${UFO_FILTERS_VERSION_MINOR}")

set(UFO_DESCRIPTION "UFO good filters")
set(UFO_DESCRIPTION_SUMMARY "UFO good filters")

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
pkg_check_modules(UFO ufo>=0.2 REQUIRED)

include_directories(
    ${GLIB2_INCLUDE_DIRS}
    ${OPENCL_INCLUDE_DIRS}
    ${UFO_INCLUDE_DIRS}
    )

add_definitions("-std=c99 -Wall -fPIC")
add_definitions(-DG_LOG_DOMAIN="Ufo")

add_subdirectory(src)

# --- Package generation ------------------------------------------------------
set(CPACK_PACKAGE_DESCRIPTION ${UFO_DESCRIPTION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${UFO_DESCRIPTION_SUMMARY})
set(CPACK_PACKAGE_NAME ${TARNAME})

set(CPACK_PACKAGE_CONTACT "matthias.vogelgesang@kit.edu")
set(CPACK_PACKAGE_VENDOR "Karlsruhe Institute of Technology/IPE")
set(CPACK_PACKAGE_VERSION ${UCA_FILTERS_VERSION_STRING_LONG})
set(CPACK_PACKAGE_VERSION_MAJOR ${UFO_FILTERS_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${UFO_FILTERS_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${UFO_FILTERS_VERSION_PATCH})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${UFO_FILTERS_VERSION_STRING_LONG}-${CMAKE_SYSTEM_PROCESSOR}")
set(VERSION ${UFO_FILTERS_VERSION})

set(CPACK_GENERATOR "DEB;RPM;")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES "tags" ".bzr" ".swp" "~1~")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${TARNAME}-${UFO_FILTERS_VERSION_STRING_LONG}" CACHE INTERNAL "tarball basename")

# --- Distro specific
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.3.6), libgcc1 (>= 1:4.1)")

include(CPack)
