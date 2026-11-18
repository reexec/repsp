// Adapted from re2DJ (BSD 3-Clause License; see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, re2DJ contributors

#ifndef REPSP_PLATFORM_POST_SHADER_SOURCE_H
#define REPSP_PLATFORM_POST_SHADER_SOURCE_H

#include <string>
#include <string_view>
#include <vector>

namespace repsp::platform
{

// One `#pragma parameter` line of a post-processing shader: a float uniform
// exposed as a slider. `value` starts at `initial`.
struct PostShaderParameter
{
    std::string name;
    std::string description;
    float initial = 0.0f;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float step = 0.0f;
    float value = 0.0f;
};

// The two stages assembled from one single-pass shader file in the libretro
// GLSL layout, translated for OpenGL 3.3 Core (or OpenGL ES 3.0).
struct PostShaderProgramSource
{
    std::string vertex;
    std::string fragment;
    std::vector<PostShaderParameter> parameters;
};

// Builds both stages from a shader file's text. Drops any leading `#version`
// line, emits `#version 330 core` (or `#version 300 es` with precision when
// `gles` is true), defines `VERTEX`/`FRAGMENT` and `PARAMETER_UNIFORM`, injects
// compatibility macros (`attribute`, `varying`, `texture2D`), and renames
// `gl_FragColor` token-by-token to a declared `out vec4` variable.
[[nodiscard]] bool BuildPostShaderProgramSource(std::string_view text,
                                                PostShaderProgramSource* source,
                                                std::string* error,
                                                bool gles = false);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_POST_SHADER_SOURCE_H
