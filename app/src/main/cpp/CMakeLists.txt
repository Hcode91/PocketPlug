cmake_minimum_required(VERSION 3.22.1)
project(pocketplug CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()
# Emulation is CPU-bound: optimise even in debug APKs
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fno-exceptions -fno-rtti")

file(GLOB_RECURSE CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cores/*.cpp)

add_library(pocketplug SHARED
    jni_bridge.cpp
    core_registry.cpp
    ${CORE_SOURCES})

target_include_directories(pocketplug PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pocketplug log)
