cmake_minimum_required(VERSION 3.25)

# Read project version from the VERSION file
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" REPSP_VERSION_STRING LIMIT_COUNT 1)
string(STRIP "${REPSP_VERSION_STRING}" REPSP_VERSION_STRING)

project(repsp
    VERSION ${REPSP_VERSION_STRING}
    DESCRIPTION "Shared portable PSP runtime library"
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(repsp_host_flags)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(REPSP_STANDALONE ON)
else()
    set(REPSP_STANDALONE OFF)
endif()

option(REPSP_BUILD_PROBES "Build repsp verification probes" ${REPSP_STANDALONE})

# Third-party dependencies
option(REPSP_SDL_SHARED "Build SDL as a shared library instead of static" OFF)

if(REPSP_SDL_SHARED)
    set(SDL_SHARED ON CACHE BOOL "Build SDL shared library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Build SDL static library" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "Build SDL shared library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Build SDL static library" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL test programs" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "Disable SDL install rules" FORCE)

add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

# Public layer targets and aliases

# Layer: platform
add_library(repsp_platform STATIC)
add_library(repsp::platform ALIAS repsp_platform)
target_include_directories(repsp_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_platform PRIVATE SDL3::SDL3)
repsp_apply_host_flags(repsp_platform)

# Layer: module
add_library(repsp_module STATIC)
add_library(repsp::module ALIAS repsp_module)
target_include_directories(repsp_module PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_module PUBLIC repsp_platform)
repsp_apply_host_flags(repsp_module)

# Layer: runtime
add_library(repsp_runtime STATIC)
add_library(repsp::runtime ALIAS repsp_runtime)
target_include_directories(repsp_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_runtime PUBLIC repsp_module repsp_platform)
repsp_apply_host_flags(repsp_runtime)

# Layer: recomp
add_library(repsp_recomp STATIC)
add_library(repsp::recomp ALIAS repsp_recomp)
target_include_directories(repsp_recomp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_recomp PUBLIC repsp_runtime)
repsp_apply_host_flags(repsp_recomp)

# Layer: gpu
add_library(repsp_gpu STATIC)
add_library(repsp::gpu ALIAS repsp_gpu)
target_include_directories(repsp_gpu PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_gpu PUBLIC repsp_runtime)
repsp_apply_host_flags(repsp_gpu)

# Layer: hle
add_library(repsp_hle_kernel STATIC)
add_library(repsp::hle_kernel ALIAS repsp_hle_kernel)
target_include_directories(repsp_hle_kernel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_kernel PUBLIC repsp_runtime)
repsp_apply_host_flags(repsp_hle_kernel)

add_library(repsp_hle_ge STATIC)
add_library(repsp::hle_ge ALIAS repsp_hle_ge)
target_include_directories(repsp_hle_ge PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_ge PUBLIC repsp_runtime repsp_gpu)
repsp_apply_host_flags(repsp_hle_ge)

add_library(repsp_hle_display STATIC)
add_library(repsp::hle_display ALIAS repsp_hle_display)
target_include_directories(repsp_hle_display PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_display PUBLIC repsp_runtime repsp_platform)
repsp_apply_host_flags(repsp_hle_display)

add_library(repsp_hle_ctrl STATIC)
add_library(repsp::hle_ctrl ALIAS repsp_hle_ctrl)
target_include_directories(repsp_hle_ctrl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_ctrl PUBLIC repsp_runtime repsp_platform)
repsp_apply_host_flags(repsp_hle_ctrl)

add_library(repsp_hle_dmac STATIC)
add_library(repsp::hle_dmac ALIAS repsp_hle_dmac)
target_include_directories(repsp_hle_dmac PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_dmac PUBLIC repsp_runtime)
repsp_apply_host_flags(repsp_hle_dmac)

add_subdirectory(src)

if(REPSP_BUILD_PROBES)
    enable_testing()
    add_subdirectory(tools)
endif()
