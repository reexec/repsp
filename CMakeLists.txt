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
option(REPSP_CODEC_FFMPEG "Build optional dynamically loaded FFmpeg codec plugin" OFF)
set(FFMPEG_ROOT "" CACHE PATH "Root directory of FFmpeg LGPL shared build")

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

# Dear ImGui (excluded from repsp strict warning flags)
set(REPSP_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
add_library(imgui STATIC
    "${REPSP_IMGUI_DIR}/imgui.cpp"
    "${REPSP_IMGUI_DIR}/imgui_draw.cpp"
    "${REPSP_IMGUI_DIR}/imgui_tables.cpp"
    "${REPSP_IMGUI_DIR}/imgui_widgets.cpp"
    "${REPSP_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${REPSP_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)
add_library(repsp::imgui ALIAS imgui)
set_target_properties(imgui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_include_directories(imgui PUBLIC
    $<BUILD_INTERFACE:${REPSP_IMGUI_DIR}>
    $<BUILD_INTERFACE:${REPSP_IMGUI_DIR}/backends>
)
target_link_libraries(imgui PRIVATE SDL3::SDL3)

# Embed built-in post-processing shaders at configure time
set(REPSP_POST_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/post_shaders")
set(REPSP_POST_SHADER_BUILTIN_NAMES
    crt.glsl
    scanline.glsl
    fsr/fsr.glslp
    fsr/easu.glsl
    fsr/rcas.glsl
    scalefx/scalefx.glslp
    scalefx/scalefx-pass0.glsl
    scalefx/scalefx-pass1.glsl
    scalefx/scalefx-pass2.glsl
    scalefx/scalefx-pass3.glsl
    scalefx/scalefx-pass4.glsl
    anime4k/anime4k.glslp
    anime4k/conv0.glsl
    anime4k/conv1.glsl
    anime4k/conv2.glsl
    anime4k/conv3.glsl
    anime4k/depth-to-space.glsl
)
set(REPSP_POST_SHADER_BUILTIN_FILES "")
foreach(name IN LISTS REPSP_POST_SHADER_BUILTIN_NAMES)
    set(path "${REPSP_POST_SHADER_SOURCE_DIR}/${name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" text)
    string(LENGTH "${text}" length)
    set(pieces "")
    set(offset 0)
    while(offset LESS length)
        string(SUBSTRING "${text}" ${offset} 12000 piece)
        string(APPEND pieces "R\"repsp_glsl(${piece})repsp_glsl\"\n")
        math(EXPR offset "${offset} + 12000")
    endwhile()
    string(APPEND REPSP_POST_SHADER_BUILTIN_FILES "    {\"${name}\",\n${pieces}    },\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${REPSP_POST_SHADER_SOURCE_DIR}/post_shader_builtins.inc.in")
configure_file(
    "${REPSP_POST_SHADER_SOURCE_DIR}/post_shader_builtins.inc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/repsp/platform/post_shader_builtins.inc"
    @ONLY
)

# Public layer targets and aliases

# Layer: platform
add_library(repsp_platform STATIC)
add_library(repsp::platform ALIAS repsp_platform)
target_include_directories(repsp_platform PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(repsp_platform PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(repsp_platform PUBLIC imgui PRIVATE SDL3::SDL3)
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

# Layer: codec
add_library(repsp_codec STATIC)
add_library(repsp::codec ALIAS repsp_codec)
target_include_directories(repsp_codec PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_codec PUBLIC repsp_runtime)
repsp_apply_host_flags(repsp_codec)

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
target_link_libraries(repsp_hle_display PUBLIC repsp_runtime repsp_platform repsp_gpu)
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

add_library(repsp_hle_audio STATIC)
add_library(repsp::hle_audio ALIAS repsp_hle_audio)
target_include_directories(repsp_hle_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_audio PUBLIC repsp_runtime repsp_platform)
repsp_apply_host_flags(repsp_hle_audio)

add_library(repsp_hle_sascore STATIC)
add_library(repsp::hle_sascore ALIAS repsp_hle_sascore)
target_include_directories(repsp_hle_sascore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_sascore PUBLIC repsp_runtime)
repsp_apply_host_flags(repsp_hle_sascore)

add_library(repsp_hle_atrac STATIC)
add_library(repsp::hle_atrac ALIAS repsp_hle_atrac)
target_include_directories(repsp_hle_atrac PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_atrac PUBLIC repsp_runtime repsp_codec)
repsp_apply_host_flags(repsp_hle_atrac)

add_library(repsp_hle_mpeg STATIC)
add_library(repsp::hle_mpeg ALIAS repsp_hle_mpeg)
target_include_directories(repsp_hle_mpeg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_mpeg PUBLIC repsp_runtime repsp_codec)
repsp_apply_host_flags(repsp_hle_mpeg)

add_library(repsp_hle_utility STATIC)
add_library(repsp::hle_utility ALIAS repsp_hle_utility)
target_include_directories(repsp_hle_utility PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(repsp_hle_utility PUBLIC repsp_runtime)
repsp_apply_host_flags(repsp_hle_utility)

add_subdirectory(src)

if(REPSP_BUILD_PROBES)
    enable_testing()
    add_subdirectory(tools)
endif()
