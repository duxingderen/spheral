# CMakeLists to build the Spheral library.

cmake_minimum_required(VERSION 3.10)
project(spheral LANGUAGES CXX Fortran)

set(SPHERAL_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SPHERAL_TEST_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})

include(cmake/SetupSpheral.cmake)

add_subdirectory(${SPHERAL_ROOT_DIR}/scripts)
