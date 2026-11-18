// Adapted from re2DJ (BSD 3-Clause License; see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, re2DJ contributors

#ifndef REPSP_PLATFORM_POST_SHADER_CATALOG_H
#define REPSP_PLATFORM_POST_SHADER_CATALOG_H

#include "repsp/platform/post_shader_preset.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace repsp::platform
{

inline constexpr const char* kPostShaderNoneId = "none";

struct PostShaderEntry
{
    std::string id;
    bool builtin = false;
    std::filesystem::path path;
};

[[nodiscard]] std::filesystem::path ResolvePostShaderDirectory(
    const std::filesystem::path& executable_directory);

[[nodiscard]] std::vector<PostShaderEntry> ListPostShaders(
    const std::filesystem::path& directory);

[[nodiscard]] const PostShaderEntry* FindPostShader(
    const std::vector<PostShaderEntry>& entries, std::string_view id);

[[nodiscard]] bool LoadPostShaderPreset(const PostShaderEntry& entry,
                                        PostShaderPreset* preset,
                                        std::string* error);

[[nodiscard]] std::string ChoosePostShader(const std::string* command_line_id,
                                           const char* environment_value,
                                           const std::string* ini_id = nullptr);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_POST_SHADER_CATALOG_H
