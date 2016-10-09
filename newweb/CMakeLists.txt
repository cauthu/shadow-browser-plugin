project(newweb)

## ensure cmake version
cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

set(MY_ROOT_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(UTILITY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/utility")

## we use cmake files from shadow
if(EXISTS "${SHADOW_ROOT}/share/cmake/Modules")
  set(CMAKE_MODULE_PATH "${SHADOW_ROOT}/share/cmake/Modules" ${CMAKE_MODULE_PATH})
endif()
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")

add_cxxflags("-std=c++11")

# set these so that the cmake FindEVENT2 and FindOPENSSL will use our
# shadow-specific versions
set(CMAKE_EXTRA_INCLUDES "${CMAKE_INSTALL_PREFIX}/include")
set(CMAKE_EXTRA_LIBRARIES "${CMAKE_INSTALL_PREFIX}/lib")

message(STATUS "CMAKE_CXX_FLAGS for newweb project = ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_EXTRA_INCLUDES for newweb project = ${CMAKE_EXTRA_INCLUDES}")
message(STATUS "CMAKE_EXTRA_LIBRARIES for newweb project = ${CMAKE_EXTRA_LIBRARIES}")


find_package(Boost REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(SPDYLAY REQUIRED)
find_package(OPENSSL REQUIRED)

if("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
  message(FATAL_ERROR "release mode would crash shadow when we try to initialize easylogging++ " 
      "(shadow can't properly handle somestatic stuff in c++). see issue " 
      "https://bitbucket.org/hatswitch/shadow-plugin-extras/issues/7/" )
endif()

# need this so we can do #include the generated flatbuffer headers;
# setting CMAKE_INCLUDE_CURRENT_DIR to ON doens't seem to work
# include_directories(AFTER ${CMAKE_CURRENT_BINARY_DIR})
include_directories(AFTER ${MY_ROOT_BUILD_DIR})

message(STATUS "Disabling easylogging verbose and debug logging\n")
add_definitions(-DELPP_NO_DEFAULT_LOG_FILE -DELPP_DISABLE_VERBOSE_LOGS -DELPP_DISABLE_DEBUG_LOGS)
add_definitions(-DELPP_DEBUG_ASSERT_FAILURE -DELPP_STACKTRACE_ON_CRASH)

add_subdirectory(utility)

## recurse to build each plug-in

add_subdirectory(browser)

add_subdirectory(webserver)

add_subdirectory(transport_proxy)
