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
add_executable(${SUITE_BIN} ${TEST_SRCS})
target_link_libraries(${SUITE_BIN} ${GLIB_LIBRARIES} ${GOBJECT_LIBRARIES})

# prefer a ufo-core instance if it is located in same folder
# as ufo-filters (usefull for development)
get_filename_component(LOCAL_UFO_DIR "${PROJECT_SOURCE_DIR}/../ufo-core/" ABSOLUTE)

set (LOCAL_UFO_LIB ${LOCAL_UFO_DIR}/ufo/libufo.so)

if (EXISTS ${LOCAL_UFO_DIR} AND IS_DIRECTORY ${LOCAL_UFO_DIR} AND EXISTS ${LOCAL_UFO_LIB})
    message("++ Integration tests: Using local UFO at: ${LOCAL_UFO_DIR}")

    include_directories(${LOCAL_UFO_DIR})
    include_directories(
        ${GOBJECT_LIBRARIES}
        ${GLIB_LIBRARIES}
    )
    link_directories(${LOCAL_UFO_DIR/ufo/})

    message("++   Linking against ${LOCAL_UFO_LIB}")
    target_link_libraries(${SUITE_BIN} ${LOCAL_UFO_LIB})
else ()
    message("++ Integration tests: Using system wide UFO installation")
    include_directories(${UFO_INCLUDE_DIRS})
    link_directories(${UFO_LIBRARY_DIRS})
    target_link_libraries(${SUITE_BIN} ufo)
endif ()



