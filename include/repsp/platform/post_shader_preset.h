// Adapted from re2DJ (BSD 3-Clause License; see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, re2DJ contributors

#ifndef REPSP_PLATFORM_POST_SHADER_PRESET_H
#define REPSP_PLATFORM_POST_SHADER_PRESET_H

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace repsp::platform
{

enum class PostShaderScaleType
{
    kSource,
    kViewport,
    kAbsolute,
};

struct PostShaderPass
{
    std::string path;
    std::string text;
    PostShaderScaleType scale_type_x = PostShaderScaleType::kSource;
    PostShaderScaleType scale_type_y = PostShaderScaleType::kSource;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    bool filter_linear = true;
    bool filter_specified = false;
    bool float_framebuffer = false;
    bool to_window = false;
};

struct PostShaderPreset
{
    std::vector<PostShaderPass> passes;
};

using PostShaderFileReader =
    std::function<bool(const std::string& relative_path, std::string* text, std::string* error)>;

[[nodiscard]] bool ParsePostShaderPreset(std::string_view text,
                                         const PostShaderFileReader& read,
                                         PostShaderPreset* preset,
                                         std::string* error);

[[nodiscard]] PostShaderPreset SinglePassPreset(std::string path, std::string text);

void PostShaderPassSize(const PostShaderPass& pass,
                        std::uint32_t input_width,
                        std::uint32_t input_height,
                        std::uint32_t viewport_width,
                        std::uint32_t viewport_height,
                        std::uint32_t* width,
                        std::uint32_t* height);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_POST_SHADER_PRESET_H
