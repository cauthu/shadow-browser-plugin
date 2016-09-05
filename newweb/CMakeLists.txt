project(shadow-plugins-extra)

## ensure cmake version
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

set(MY_ROOT_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(UTILITY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/utility")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

add_definitions(-DELPP_DEBUG_ASSERT_FAILURE -DELPP_STACKTRACE_ON_CRASH)

add_subdirectory(utility)

## recurse to build each plug-in

add_subdirectory(browser)
# add_subdirectory(webserver)
