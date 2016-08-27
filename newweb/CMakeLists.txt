project(shadow-plugins-extra)

## ensure cmake version
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")

## recurse to build each plug-in

add_subdirectory(browser)
# add_subdirectory(webserver)
