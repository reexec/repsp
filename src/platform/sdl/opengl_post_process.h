// Adapted from re2DJ (BSD 3-Clause License; see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, re2DJ contributors

#ifndef REPSP_PLATFORM_SDL_OPENGL_POST_PROCESS_H
#define REPSP_PLATFORM_SDL_OPENGL_POST_PROCESS_H

#include "repsp/platform/post_shader.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace repsp::platform::sdl_internal
{

class OpenGlPostProcess final : public PostShaderControl
{
public:
    static constexpr std::uint32_t kVertexCoordSlot = 0;
    static constexpr std::uint32_t kColorSlot = 1;
    static constexpr std::uint32_t kTexCoordSlot = 2;

    OpenGlPostProcess();
    ~OpenGlPostProcess() override;

    OpenGlPostProcess(const OpenGlPostProcess&) = delete;
    OpenGlPostProcess& operator=(const OpenGlPostProcess&) = delete;

    bool Initialize(const std::filesystem::path& shader_dir, bool is_es, std::string* message);
    void Shutdown();

    bool active() const;
    std::size_t pass_count() const;

    bool UseForPresent(std::uint32_t source_texture,
                       std::uint32_t input_width,
                       std::uint32_t input_height,
                       std::uint32_t logical_width,
                       std::uint32_t logical_height,
                       std::int32_t viewport_x,
                       std::int32_t viewport_y,
                       std::uint32_t output_width,
                       std::uint32_t output_height,
                       bool flip_y);
    void DrawLastQuad();

    const std::vector<PostShaderEntry>& catalog() const override;
    const std::string& shader_directory() const override;
    const std::string& active_id() const override;
    const std::string& last_error() const override;
    bool Select(std::string_view id) override;
    bool Reload() override;
    std::vector<PostShaderParameter>& parameters() override;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace repsp::platform::sdl_internal

#endif // REPSP_PLATFORM_SDL_OPENGL_POST_PROCESS_H
