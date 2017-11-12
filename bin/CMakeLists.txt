cmake_minimum_required(VERSION 2.6)

include_directories(ufo-benchmarks ${UFO_INCLUDE_DIRS})

add_executable(ufo-benchmarks ufo-benchmarks.c)
target_link_libraries(ufo-benchmarks ${UFO_LIBRARIES})

install(TARGETS ufo-benchmarks RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
