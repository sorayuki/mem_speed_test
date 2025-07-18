cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(opencltest)

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/OpenCL-SDK-v2024.05.08-Win-x64)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/angle-win-x64)
# list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/mesa3d-24.3.2-devel-msvc)

add_library(LibCL INTERFACE)
find_library(LIB_CL OpenCL REQUIRED)
find_path(INC_CL CL/opencl.hpp REQUIRED)
target_link_libraries(LibCL INTERFACE ${LIB_CL})
target_include_directories(LibCL INTERFACE ${INC_CL})
target_compile_definitions(LibCL INTERFACE -DCL_VERSION_2_0)

add_library(LibGLES INTERFACE)
find_library(LIB_EGL EGL REQUIRED)
find_library(LIB_GLESV2 GLESv2 REQUIRED)
find_path(INC_GLES GLES3/gl31.h REQUIRED)
target_link_libraries(LibGLES INTERFACE ${LIB_EGL} ${LIB_GLESV2})
target_include_directories(LibGLES INTERFACE ${INC_GLES})

add_executable(test)
target_sources(test PRIVATE main.cpp)
target_link_libraries(test PRIVATE LibCL)

add_executable(empty)
target_sources(empty PRIVATE empty_kernel.cpp)
target_link_libraries(empty PRIVATE LibCL)

add_executable(colorconv)
target_sources(colorconv PRIVATE colorconv.cpp)
target_link_libraries(colorconv PRIVATE LibCL)

add_executable(colorconv_gles)
target_sources(colorconv_gles PRIVATE colorconv_runtest.h colorconv_gles.cpp)
target_link_libraries(colorconv_gles PRIVATE LibGLES)

add_executable(colorconv_dx)
target_sources(colorconv_dx PRIVATE colorconv_runtest.h colorconv_dx.cpp)
target_link_libraries(colorconv_dx PRIVATE d3d11 d3dcompiler dxgi)