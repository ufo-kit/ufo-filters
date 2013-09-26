cmake_minimum_required(VERSION 2.6)
project(ufo C)

set(TARNAME "ufo-filters")

set(UFO_FILTERS_VERSION_MAJOR "0")
set(UFO_FILTERS_VERSION_MINOR "5")
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
set(PKG_UFO_CORE_MIN_REQUIRED "0.5")

find_package(OpenCL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UFO ufo>=${PKG_UFO_CORE_MIN_REQUIRED} REQUIRED)

#{{{ Plugin-specific dependencies
find_package(TIFF)
pkg_check_modules(OCLFFT oclfft)
#}}}

include_directories(
    ${OPENCL_INCLUDE_DIRS}
    ${UFO_INCLUDE_DIRS})

#{{{ Global definitions
add_definitions("-std=c99 -pedantic -Wall -Wextra -fPIC")
add_definitions(-DG_LOG_DOMAIN="Ufo")

if (CMAKE_COMPILER_IS_GNUCC)
    add_definitions("-Wmissing-prototypes -Wmissing-declarations -Wshadow
    -Wpointer-arith -Wcast-align -Wwrite-strings -Wredundant-decls -Wcast-qual
    -Wnested-externs -Winline -Wno-long-long -Wconversion -Wstrict-prototypes")

    add_definitions("-Wno-unused-parameter -Wno-missing-field-initializers")
endif()
#}}}

#{{{ Subdirectories
add_subdirectory(src)
add_subdirectory(tests/integration_tests)
#}}}

#{{{ CPack
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

#{{{ Debian
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libufo (>= ${PKG_UFO_CORE_MIN_REQUIRED})")

if (TIFF_FOUND)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libtiff4")
endif()

if (OCLFFT_FOUND)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, liboclfft (>= 1.0)")
endif()
#}}}

include(CPack)
#}}}
