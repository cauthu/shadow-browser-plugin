project(shadow-plugins-extra)

## ensure cmake version
cmake_minimum_required(VERSION 3.0.2 FATAL_ERROR)

set(MY_ROOT_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(UTILITY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/utility")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

find_package(Boost REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(SPDYLAY REQUIRED)
find_package(OPENSSL REQUIRED)

# need this so we can do #include the generated flatbuffer headers;
# setting CMAKE_INCLUDE_CURRENT_DIR to ON doens't seem to work
# include_directories(AFTER ${CMAKE_CURRENT_BINARY_DIR})
include_directories(AFTER ${MY_ROOT_BUILD_DIR})

add_definitions(-DELPP_DEBUG_ASSERT_FAILURE -DELPP_STACKTRACE_ON_CRASH)

add_subdirectory(utility)

## recurse to build each plug-in

add_subdirectory(browser)
# add_subdirectory(webserver)
