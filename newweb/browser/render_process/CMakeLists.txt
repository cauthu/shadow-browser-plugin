cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(browser CXX)

set(CMAKE_VERBOSE_MAKEFILE on)

## Browser-Simulator

## Find libtidy which is needed to parse HTML
find_package(Boost REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(SPDYLAY REQUIRED)
find_package(OPENSSL REQUIRED)

set(UTILITY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../utility")

include_directories(AFTER ${SPDYLAY_INCLUDES} ${OPENSSL_INCLUDES} ${EVENT2_INCLUDES})

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(RENDER_PROCESS_SOURCES
  render_process.cpp
  ipc.cpp
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/json_stream_channel.cpp
)

set(LINK_LIBS ${EVENT2_LIBRARIES} ${SPDYLAY_LIBRARIES})

## create and install a shared library that can plug into shadow
add_shadow_plugin(shadow-plugin-render_process ${RENDER_PROCESS_SOURCES})
target_link_libraries(shadow-plugin-render_process ${LINK_LIBS})
install(TARGETS shadow-plugin-render_process DESTINATION plugins)

## create and install an executable that can run outside of shadow
add_executable(render_process ${RENDER_PROCESS_SOURCES})
target_link_libraries(render_process ${LINK_LIBS})
install(TARGETS render_process DESTINATION bin)

