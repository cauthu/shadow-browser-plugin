
include_directories(AFTER ${EVENT2_INCLUDES} ${OPENSSL_INCLUDES})

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(WEBSERVER_SOURCES
  webserver.cc
  handler.cc
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/stream_channel.cpp
  ${UTILITY_DIR}/timer.cpp
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/tcp_server.cpp
  ${UTILITY_DIR}/object.cpp
)

set(LINK_LIBS ${EVENT2_LIBRARIES} ${OPENSSL_LIBRARIES})


if(NOT "${CMAKE_SKIP_PLUGINS}" STREQUAL "yes")

  ## create and install a shared library that can plug into shadow
  add_definitions(-DIN_SHADOW)
  add_shadow_plugin(shadow-plugin-webserver ${WEBSERVER_SOURCES})
  target_link_libraries(shadow-plugin-webserver ${LINK_LIBS})
  install(TARGETS shadow-plugin-webserver DESTINATION plugins)

endif()


if("${CMAKE_BUILD_NATIVE_BINARIES}" STREQUAL "yes")

  ## create and install an executable that can run outside of shadow
  remove_definitions(-DIN_SHADOW)
  add_executable(webserver ${WEBSERVER_SOURCES})
  target_link_libraries(webserver ${LINK_LIBS})
  install(TARGETS webserver DESTINATION bin)

endif()
