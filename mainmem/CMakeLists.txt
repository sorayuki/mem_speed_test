cmake_minimum_required(VERSION 3.16)
project(memcpytest)

add_executable(memcpytest)
target_sources(memcpytest PRIVATE
    main.cpp
)
target_include_directories(memcpytest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libsimdpp-2.1
    ${CMAKE_CURRENT_SOURCE_DIR}/FastMemcpy-master
)
set_property(TARGET memcpytest PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)