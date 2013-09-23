cmake_minimum_required(VERSION 2.8)

# configure unit tests
set(TEST_SRCS
    test-suite.c
    test-complete-local-setup.c
    test-complete-remote-setup.c
)

set(SUITE_BIN "test-suite")
add_executable(${SUITE_BIN} ${TEST_SRCS})

include_directories(
    ${GLIB2_INCLUDE_DIRS}
)

# prefer a ufo-core instance if it is located in same folder
# as ufo-filters (usefull for development)
get_filename_component(LOCAL_UFO_DIR "${PROJECT_SOURCE_DIR}/../ufo-core/" ABSOLUTE)

set (LOCAL_UFO_LIB ${LOCAL_UFO_DIR}/ufo/libufo.so)

if (EXISTS ${LOCAL_UFO_DIR} AND IS_DIRECTORY ${LOCAL_UFO_DIR} AND EXISTS ${LOCAL_UFO_LIB})
    message ("Using local UFO at: ${LOCAL_UFO_DIR}")

    include_directories(${LOCAL_UFO_DIR})
    link_directories(${LOCAL_UFO_DIR/ufo/})

    message("Linking against ${LOCAL_UFO_LIB}")
    target_link_libraries(${SUITE_BIN}
        ${LOCAL_UFO_LIB}
    )
else ()
    message ("Using system wide UFO installation")
    include_directories(
        ${UFO_INCLUDE_DIRS}
    )
    link_directories(
        ${UFO_LIBRARY_DIRS}
    )
    target_link_libraries(${SUITE_BIN}
        ufo
    )
endif ()



