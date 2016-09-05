cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

# project(browser CXX)

## Browser-Simulator

## Find libtidy which is needed to parse HTML
find_package(Boost REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(SPDYLAY REQUIRED)
find_package(OPENSSL REQUIRED)

include_directories(AFTER ${SPDYLAY_INCLUDES} ${OPENSSL_INCLUDES} ${EVENT2_INCLUDES})

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(IO_PROCESS_SOURCES
  io_process.cpp
  ipc.cpp
  ${UTILITY_DIR}/socks5_connector.cpp
  ${UTILITY_DIR}/connection.cc
  ${UTILITY_DIR}/connection_manager.cc
  ${UTILITY_DIR}/request.cc
  ${UTILITY_DIR}/http_parse.c
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/tcp_server.cpp
  ${UTILITY_DIR}/json_stream_channel.cpp
  ${UTILITY_DIR}/object.cpp
)

set(LINK_LIBS ${EVENT2_LIBRARIES} ${SPDYLAY_LIBRARIES})

# need this so we can do #include the generated flatbuffer headers;
# setting CMAKE_INCLUDE_CURRENT_DIR to ON doens't seem to work
# include_directories(AFTER ${CMAKE_CURRENT_BINARY_DIR})
include_directories(AFTER ${MY_ROOT_BUILD_DIR})


## create and install a shared library that can plug into shadow
add_definitions(-DIN_SHADOW)
add_shadow_plugin(shadow-plugin-io_process ${IO_PROCESS_SOURCES})
target_link_libraries(shadow-plugin-io_process ${LINK_LIBS})
install(TARGETS shadow-plugin-io_process DESTINATION plugins)

# "add_dependencies(shadow-plugin-io_process" won't work because
# building the bitcode will fail without the
# ioservice_messages_generated.h
add_dependencies(shadow-plugin-io_process-bitcode io_service_ipc_messages_flatbuffers)



## create and install an executable that can run outside of shadow
remove_definitions(-DIN_SHADOW)
add_executable(io_process ${IO_PROCESS_SOURCES})
target_link_libraries(io_process ${LINK_LIBS})
install(TARGETS io_process DESTINATION bin)

add_dependencies(io_process io_service_ipc_messages_flatbuffers)
