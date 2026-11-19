cmake_minimum_required(VERSION 3.22.1)
project(pocketplug C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()
# Emulation is CPU-bound: optimise even in debug APKs
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fno-exceptions -fno-rtti")

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cores/*.cpp)

# UNIX PC (3B1): FreeBee (GPL-3) and Musashi (MIT) are C
set(UNIXPC ${CMAKE_CURRENT_SOURCE_DIR}/cores/unixpc/freebee)
file(GLOB UNIXPC_C_SOURCES CONFIGURE_DEPENDS ${UNIXPC}/*.c)
list(APPEND UNIXPC_C_SOURCES
    ${UNIXPC}/musashi/m68kcpu.c
    ${UNIXPC}/musashi/m68kops.c
    ${UNIXPC}/musashi/m68kdasm.c
    ${UNIXPC}/musashi/softfloat/softfloat.c)
set_source_files_properties(${UNIXPC_C_SOURCES} PROPERTIES
    COMPILE_FLAGS "-std=gnu99 -O2 -w")

# Atari 1200XL / 130XE: Atari800 (GPL-2+) as libatari800, in C
set(A800 ${CMAKE_CURRENT_SOURCE_DIR}/cores/atari800/a800)
file(GLOB A800_C_SOURCES CONFIGURE_DEPENDS ${A800}/*.c ${A800}/libatari800/*.c ${A800}/roms/*.c)
set_source_files_properties(${A800_C_SOURCES} PROPERTIES
    COMPILE_FLAGS "-std=gnu99 -O2 -w -I${A800}")

# Acorn A7000 / A7000+: RPCEmu (GPL-2+), interpreter build, in C
set(ACORN ${CMAKE_CURRENT_SOURCE_DIR}/cores/acorn)
file(GLOB ACORN_C_SOURCES CONFIGURE_DEPENDS ${ACORN}/*.c ${ACORN}/rpcemu/*.c
    ${ACORN}/rpcemu/filesystem/*.c ${ACORN}/rpcemu/platform/*.c)
set_source_files_properties(${ACORN_C_SOURCES} PROPERTIES
    COMPILE_FLAGS "-std=gnu99 -O2 -w -DRPCEMU_PLATFORM_LINUX -I${ACORN}/rpcemu -I${ACORN}/rpcemu/filesystem -I${ACORN}/rpcemu/platform")

add_library(pocketplug SHARED
    jni_bridge.cpp
    core_registry.cpp
    ${CORE_SOURCES}
    ${UNIXPC_C_SOURCES}
    ${A800_C_SOURCES}
    ${ACORN_C_SOURCES})

target_include_directories(pocketplug PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pocketplug PRIVATE POCKETPLUG "MUSASHI_CNF=\"../m68kconf.h\"")
target_link_libraries(pocketplug log)
