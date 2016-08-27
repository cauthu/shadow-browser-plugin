cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(browser CXX)

## Browser-Simulator

## Find libtidy which is needed to parse HTML
find_package(Boost REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(SPDYLAY REQUIRED)
find_package(OPENSSL REQUIRED)

set(UTILITY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../utility")

include_directories(AFTER ${SPDYLAY_INCLUDES} ${OPENSSL_INCLUDES} ${EVENT2_INCLUDES})

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(IOPROCESS_SOURCES
  IOProcess.cpp
  ipc.cpp
  ${UTILITY_DIR}/myevent.cc
  ${UTILITY_DIR}/connection.cc
  ${UTILITY_DIR}/connection_manager.cc
  ${UTILITY_DIR}/request.cc
  ${UTILITY_DIR}/http_parse.c
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/tcp_server.cpp
  ${UTILITY_DIR}/json_stream_channel.cpp
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(LINK_LIBS ${EVENT2_LIBRARIES} ${SPDYLAY_LIBRARIES})


## create and install a shared library that can plug into shadow
add_shadow_plugin(shadow-plugin-ioprocess ${IOPROCESS_SOURCES})
target_link_libraries(shadow-plugin-ioprocess ${LINK_LIBS})
install(TARGETS shadow-plugin-ioprocess DESTINATION plugins)

# ## create and install an executable that can run outside of shadow
# add_executable(ioprocess ${IOPROCESS_SOURCES})
# target_link_libraries(ioprocess ${LINK_LIBS})
# install(TARGETS ioprocess DESTINATION bin)

