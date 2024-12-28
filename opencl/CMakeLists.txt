cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(opencltest)

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/OpenCL-SDK-v2024.05.08-Win-x64)

find_library(LIB_CL OpenCL REQUIRED)
find_path(INC_CL CL/opencl.hpp REQUIRED)

add_library(LibCL INTERFACE)
target_link_libraries(LibCL INTERFACE ${LIB_CL})
target_include_directories(LibCL INTERFACE ${INC_CL})
target_compile_definitions(LibCL INTERFACE -DCL_VERSION_2_0)

add_executable(test)
target_sources(test PRIVATE main.cpp)
target_link_libraries(test PRIVATE LibCL)

add_executable(empty)
target_sources(empty PRIVATE empty_kernel.cpp)
target_link_libraries(empty PRIVATE LibCL)

add_executable(colorconv)
target_sources(colorconv PRIVATE colorconv.cpp)
target_link_libraries(colorconv PRIVATE LibCL)