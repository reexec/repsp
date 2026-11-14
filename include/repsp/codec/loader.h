#ifndef REPSP_CODEC_LOADER_H
#define REPSP_CODEC_LOADER_H

#include "repsp/codec/atrac_decoder.h"
#include "repsp/codec/avc_decoder.h"
#include "repsp/codec/plugin.h"
#include "repsp/platform/dynlib.h"

#include <filesystem>
#include <memory>
#include <string>

namespace repsp::codec
{

struct CodecPluginLoadResult
{
    bool ok = false;
    std::string error;
};

class LoadedCodecPlugin
{
public:
    LoadedCodecPlugin() = default;
    ~LoadedCodecPlugin();

    LoadedCodecPlugin(const LoadedCodecPlugin&) = delete;
    LoadedCodecPlugin& operator=(const LoadedCodecPlugin&) = delete;
    LoadedCodecPlugin(LoadedCodecPlugin&&) noexcept = default;
    LoadedCodecPlugin& operator=(LoadedCodecPlugin&&) noexcept = default;

    bool IsLoaded() const { return table_ != nullptr; }
    const char* Name() const;
    const char* Description() const;

    AtracDecoder* GetAtracDecoder();
    AvcDecoder* GetAvcDecoder();

private:
    friend std::unique_ptr<LoadedCodecPlugin> LoadCodecPlugin(
        const std::filesystem::path& path,
        std::string* error_out);

    platform::DynamicLibrary library_;
    const RepspCodecPluginV1* table_ = nullptr;
    std::unique_ptr<AtracDecoder> atrac_decoder_;
    std::unique_ptr<AvcDecoder> avc_decoder_;
};

std::unique_ptr<LoadedCodecPlugin> LoadCodecPlugin(
    const std::filesystem::path& path,
    std::string* error_out = nullptr);

std::filesystem::path DefaultCodecPluginFilename();

} // namespace repsp::codec

#endif // REPSP_CODEC_LOADER_H
