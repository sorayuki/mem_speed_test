cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(opencltest)

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/OpenCL-SDK-v2024.05.08-Win-x64)

find_library(LIB_CL OpenCL REQUIRED)
find_path(INC_CL CL/opencl.hpp REQUIRED)

add_executable(test)
target_sources(test PRIVATE main.cpp)
target_include_directories(test PRIVATE ${INC_CL})
target_link_libraries(test PRIVATE ${LIB_CL})

add_executable(empty)
target_sources(empty PRIVATE empty_kernel.cpp)
target_include_directories(empty PRIVATE ${INC_CL})
target_link_libraries(empty PRIVATE ${LIB_CL})

add_executable(colorconv)
target_sources(colorconv PRIVATE colorconv.cpp)
target_include_directories(colorconv PRIVATE ${INC_CL})
target_link_libraries(colorconv PRIVATE ${LIB_CL})