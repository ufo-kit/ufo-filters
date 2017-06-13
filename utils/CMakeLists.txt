cmake_minimum_required(VERSION 2.6)

set(rofex_configure_LIBS
    m
    ${UFO_LIBRARIES}
   )
#{{{ Binaries
add_executable(rofex-configure rofex-configure.c)

target_link_libraries(rofex-configure ${rofex_configure_LIBS})

include_directories(${UFO_INCLUDE_DIRS})

install(TARGETS rofex-configure
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
#}}}
