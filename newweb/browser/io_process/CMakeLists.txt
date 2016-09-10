## Browser-Simulator

include_directories(AFTER ${SPDYLAY_INCLUDES} ${OPENSSL_INCLUDES} ${EVENT2_INCLUDES})

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(IO_PROCESS_SOURCES
  io_process.cpp
  ipc.cpp
  http_session.cpp
  ${UTILITY_DIR}/stream_channel.cpp
  ${UTILITY_DIR}/timer.cpp
  ${UTILITY_DIR}/socks5_connector.cpp
  ${UTILITY_DIR}/connection.cc
  ${UTILITY_DIR}/connection_manager.cc
  ${UTILITY_DIR}/request.cc
  ${UTILITY_DIR}/http_parse.c
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/tcp_server.cpp
  ${UTILITY_DIR}/generic_message_channel.cpp
  ${UTILITY_DIR}/object.cpp
)

set(LINK_LIBS ${EVENT2_LIBRARIES} ${SPDYLAY_LIBRARIES})


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
