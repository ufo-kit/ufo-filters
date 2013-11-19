cmake_minimum_required(VERSION 2.8)

# configure unit tests
set(TEST_SRCS
    test-suite.c
    test-complete-local-setup.c
    test-complete-remote-setup.c
)

pkg_check_modules(GLIB glib-2.0 REQUIRED)
pkg_check_modules(GOBJECT gobject-2.0 REQUIRED)

set(SUITE_BIN "test-suite")
include_directories(${UFO_INCLUDE_DIRS}
                    ${GLIB_INCLUDE_DIRS}
                    ${GOBJECT_INCLUDE_DIRS})
add_executable(${SUITE_BIN} ${TEST_SRCS})

link_directories(${UFO_LIBRARY_DIRS})
target_link_libraries(${SUITE_BIN}
                      ${GLIB_LIBRARIES}
                      ${GOBJECT_LIBRARIES}
                      ufo)
