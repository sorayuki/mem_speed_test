cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(opencltest)

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/OpenCL-SDK-v2024.05.08-Win-x64)

find_library(LIB_CL OpenCL REQUIRED)
find_path(INC_CL CL/opencl.hpp REQUIRED)

add_executable(test)
target_sources(test PRIVATE main.cpp)
target_include_directories(test PRIVATE ${INC_CL})
target_link_libraries(test PRIVATE ${LIB_CL})