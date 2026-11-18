// Adapted from re2DJ (BSD 3-Clause License; see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, re2DJ contributors

#ifndef REPSP_PLATFORM_POST_SHADER_H
#define REPSP_PLATFORM_POST_SHADER_H

#include "repsp/platform/post_shader_catalog.h"
#include "repsp/platform/post_shader_source.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace repsp::platform
{

// Runtime control interface for the presentation post-processing shader pass.
// Every method is called only on the UI thread while the OpenGL context is current.
class PostShaderControl
{
public:
    virtual ~PostShaderControl() = default;

    virtual const std::vector<PostShaderEntry>& catalog() const = 0;
    virtual const std::string& shader_directory() const = 0;

    virtual const std::string& active_id() const = 0;
    virtual const std::string& last_error() const = 0;

    virtual bool Select(std::string_view id) = 0;
    virtual bool Reload() = 0;

    virtual std::vector<PostShaderParameter>& parameters() = 0;
};

// Configured by the app before opening the display window.
void SetPostShaderDirectory(const std::filesystem::path& directory);
std::filesystem::path GetPostShaderDirectory();
void SetInitialPostShader(std::string id);

// Returns the active control object on the UI thread when an OpenGL context is
// active, or nullptr when no GL context exists (software renderer / headless).
PostShaderControl* GetPostShaderControl();

} // namespace repsp::platform

#endif // REPSP_PLATFORM_POST_SHADER_H
