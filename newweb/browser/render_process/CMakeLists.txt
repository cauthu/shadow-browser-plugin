set(CMAKE_VERBOSE_MAKEFILE on)

include_directories(AFTER ${SPDYLAY_INCLUDES} ${OPENSSL_INCLUDES} ${EVENT2_INCLUDES})

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(RENDER_PROCESS_SOURCES
  main.cpp
  ipc_io_service.cpp
  ipc_renderer.cpp
  ${UTILITY_DIR}/object.cpp
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/stream_channel.cpp
  ${UTILITY_DIR}/timer.cpp
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/tcp_server.cpp
  ${UTILITY_DIR}/generic_message_channel.cpp
  ${UTILITY_DIR}/ipc/generic_ipc_channel.cpp
)

set(LINK_LIBS ${EVENT2_LIBRARIES})

set(IPC_MSG_BUFS
  io_service_ipc_messages_flatbuffers
  renderer_ipc_messages_flatbuffers
)

## create and install a shared library that can plug into shadow
add_definitions(-DIN_SHADOW)
add_shadow_plugin(shadow-plugin-render_process ${RENDER_PROCESS_SOURCES})
target_link_libraries(shadow-plugin-render_process ${LINK_LIBS})
install(TARGETS shadow-plugin-render_process DESTINATION plugins)

add_dependencies(
  shadow-plugin-render_process-bitcode
  ${IPC_MSG_BUFS}
)

## create and install an executable that can run outside of shadow
remove_definitions(-DIN_SHADOW)
add_executable(render_process ${RENDER_PROCESS_SOURCES})
target_link_libraries(render_process ${LINK_LIBS})
install(TARGETS render_process DESTINATION bin)

add_dependencies(
  render_process
  ${IPC_MSG_BUFS}
)
