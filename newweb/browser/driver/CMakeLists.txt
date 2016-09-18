set(CMAKE_VERBOSE_MAKEFILE on)

include_directories(AFTER ${SPDYLAY_INCLUDES} ${OPENSSL_INCLUDES} ${EVENT2_INCLUDES})

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(DRIVER_PROCESS_SOURCES
  main.cpp
  driver.cpp
  driver_tproxy.cpp
  ${UTILITY_DIR}/object.cpp
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/stream_channel.cpp
  ${UTILITY_DIR}/timer.cpp
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/generic_message_channel.cpp
  ${UTILITY_DIR}/ipc/generic_ipc_channel.cpp
)

set(LINK_LIBS ${EVENT2_LIBRARIES})

## create and install a shared library that can plug into shadow
add_definitions(-DIN_SHADOW)
add_shadow_plugin(shadow-plugin-driver_process ${DRIVER_PROCESS_SOURCES})
target_link_libraries(shadow-plugin-driver_process ${LINK_LIBS})
install(TARGETS shadow-plugin-driver_process DESTINATION plugins)

add_dependencies(
  shadow-plugin-driver_process-bitcode
  transport_proxy_ipc_messages_flatbuffers)


# ## create and install an executable that can run outside of shadow
# remove_definitions(-DIN_SHADOW)
# add_executable(driver_process ${DRIVER_PROCESS_SOURCES})
# target_link_libraries(driver_process ${LINK_LIBS})
# install(TARGETS driver_process DESTINATION bin)

# add_dependencies(
#   driver_process
#   transport_proxy_ipc_messages_flatbuffers)

