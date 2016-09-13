
include_directories(AFTER ${SPDYLAY_INCLUDES} ${OPENSSL_INCLUDES} ${EVENT2_INCLUDES})

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(TRANSPORT_PROXY_SOURCES
  main.cpp
  csp/csp.cpp
  csp/client_handler.cpp
  ssp/ssp.cpp
  ssp/csp_handler.cpp
  ssp/stream_handler.cpp
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/stream_channel.cpp
  ${UTILITY_DIR}/timer.cpp
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/tcp_server.cpp
  ${UTILITY_DIR}/socks5_connector.cpp
  ${UTILITY_DIR}/buflo_mux_channel_impl_spdy.cpp
  ${UTILITY_DIR}/object.cpp
)

set(LINK_LIBS ${EVENT2_LIBRARIES} ${SPDYLAY_LIBRARIES})


## create and install a shared library that can plug into shadow
add_definitions(-DIN_SHADOW)
add_shadow_plugin(shadow-plugin-transport_proxy ${TRANSPORT_PROXY_SOURCES})
target_link_libraries(shadow-plugin-transport_proxy ${LINK_LIBS})
install(TARGETS shadow-plugin-transport_proxy DESTINATION plugins)



## create and install an executable that can run outside of shadow
remove_definitions(-DIN_SHADOW)
add_executable(transport_proxy ${TRANSPORT_PROXY_SOURCES})
target_link_libraries(transport_proxy ${LINK_LIBS})
install(TARGETS transport_proxy DESTINATION bin)