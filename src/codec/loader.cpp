#include "repsp/codec/loader.h"

namespace repsp::codec
{

namespace
{

class PluginAtracDecoder final : public AtracDecoder
{
public:
    explicit PluginAtracDecoder(const RepspCodecPluginV1* table)
        : table_(table)
    {
    }

    void* CreateStreamContext() override
    {
        if (table_ != nullptr && table_->atrac_create != nullptr)
        {
            return table_->atrac_create();
        }
        return nullptr;
    }

    void DestroyStreamContext(void* ctx) override
    {
        if (table_ != nullptr && table_->atrac_destroy != nullptr && ctx != nullptr)
        {
            table_->atrac_destroy(ctx);
        }
    }

    void ResetStreamContext(void* ctx) override
    {
        if (table_ != nullptr && table_->atrac_reset != nullptr && ctx != nullptr)
        {
            table_->atrac_reset(ctx);
        }
    }

    bool DecodeFrame(void* ctx,
                     const AtracFrameInfo& info,
                     const uint8_t* frame_bytes,
                     size_t frame_size,
                     int16_t* pcm_out,
                     size_t out_samples) override
    {
        if (table_ == nullptr || table_->atrac_decode == nullptr)
        {
            return false;
        }
        RepspAtracFrameInfo c_info{};
        c_info.format_tag = info.format_tag;
        c_info.channels = info.channels;
        c_info.sample_rate = info.sample_rate;
        c_info.samples_per_frame = info.samples_per_frame;
        c_info.block_align = info.block_align;
        c_info.extradata = info.extradata.empty() ? nullptr : info.extradata.data();
        c_info.extradata_size = static_cast<uint32_t>(info.extradata.size());
        c_info.flags = 0;
        return table_->atrac_decode(ctx, &c_info, frame_bytes, frame_size, pcm_out, out_samples) != 0;
    }

    bool DecodeFrame(const AtracFrameInfo& info,
                     const uint8_t* frame_bytes,
                     size_t frame_size,
                     int16_t* pcm_out,
                     size_t out_samples) override
    {
        return DecodeFrame(nullptr, info, frame_bytes, frame_size, pcm_out, out_samples);
    }

    const char* Name() const override
    {
        return (table_ != nullptr && table_->name != nullptr) ? table_->name : "plugin";
    }

private:
    const RepspCodecPluginV1* table_ = nullptr;
};

class PluginAvcDecoder final : public AvcDecoder
{
public:
    explicit PluginAvcDecoder(const RepspCodecPluginV1* table)
        : table_(table)
    {
    }

    void* CreateStreamContext() override
    {
        if (table_ != nullptr && table_->avc_create != nullptr)
        {
            return table_->avc_create();
        }
        return nullptr;
    }

    void DestroyStreamContext(void* ctx) override
    {
        if (table_ != nullptr && table_->avc_destroy != nullptr && ctx != nullptr)
        {
            table_->avc_destroy(ctx);
        }
    }

    void Flush(void* ctx) override
    {
        if (table_ != nullptr && table_->avc_flush != nullptr && ctx != nullptr)
        {
            table_->avc_flush(ctx);
        }
    }

    bool DecodeAu(void* ctx,
                  std::span<const uint8_t> au_bytes,
                  uint32_t width,
                  uint32_t height,
                  uint32_t pixel_format,
                  std::span<uint8_t> out) override
    {
        if (table_ == nullptr || table_->avc_decode == nullptr)
        {
            return false;
        }
        return table_->avc_decode(
                   ctx,
                   au_bytes.empty() ? nullptr : au_bytes.data(),
                   au_bytes.size(),
                   width,
                   height,
                   pixel_format,
                   out.empty() ? nullptr : out.data(),
                   out.size()) != 0;
    }

    bool DecodeAu(std::span<const uint8_t> au_bytes,
                  uint32_t width,
                  uint32_t height,
                  uint32_t pixel_format,
                  std::span<uint8_t> out) override
    {
        return DecodeAu(nullptr, au_bytes, width, height, pixel_format, out);
    }

    const char* Name() const override
    {
        return (table_ != nullptr && table_->name != nullptr) ? table_->name : "plugin";
    }

private:
    const RepspCodecPluginV1* table_ = nullptr;
};

} // namespace

LoadedCodecPlugin::~LoadedCodecPlugin()
{
    atrac_decoder_.reset();
    avc_decoder_.reset();
    table_ = nullptr;
    library_.Close();
}

const char* LoadedCodecPlugin::Name() const
{
    return (table_ != nullptr && table_->name != nullptr) ? table_->name : "";
}

const char* LoadedCodecPlugin::Description() const
{
    return (table_ != nullptr && table_->description != nullptr) ? table_->description : "";
}

AtracDecoder* LoadedCodecPlugin::GetAtracDecoder()
{
    return atrac_decoder_.get();
}

AvcDecoder* LoadedCodecPlugin::GetAvcDecoder()
{
    return avc_decoder_.get();
}

std::unique_ptr<LoadedCodecPlugin> LoadCodecPlugin(
    const std::filesystem::path& path,
    std::string* error_out)
{
    platform::DynamicLibrary lib;
    std::string dl_error;
    if (!lib.Open(path, &dl_error))
    {
        if (error_out != nullptr)
        {
            *error_out = "failed to open codec plugin: " + dl_error;
        }
        return nullptr;
    }

    auto get_v1 = lib.GetSymbolAs<RepspCodecPluginGetV1Fn>(REPSP_CODEC_PLUGIN_GET_V1_SYMBOL);
    if (get_v1 == nullptr)
    {
        if (error_out != nullptr)
        {
            *error_out = "missing symbol " REPSP_CODEC_PLUGIN_GET_V1_SYMBOL;
        }
        return nullptr;
    }

    const RepspCodecPluginV1* table = get_v1();
    if (table == nullptr)
    {
        if (error_out != nullptr)
        {
            *error_out = "plugin returned null descriptor";
        }
        return nullptr;
    }

    if (table->abi_version != REPSP_CODEC_PLUGIN_ABI_VERSION)
    {
        if (error_out != nullptr)
        {
            *error_out = "unsupported plugin ABI version " + std::to_string(table->abi_version);
        }
        return nullptr;
    }

    if (table->struct_size < sizeof(RepspCodecPluginV1))
    {
        if (error_out != nullptr)
        {
            *error_out = "plugin descriptor struct_size too small";
        }
        return nullptr;
    }

    auto loaded = std::make_unique<LoadedCodecPlugin>();
    loaded->library_ = std::move(lib);
    loaded->table_ = table;
    if (table->atrac_decode != nullptr)
    {
        loaded->atrac_decoder_ = std::make_unique<PluginAtracDecoder>(table);
    }
    if (table->avc_decode != nullptr)
    {
        loaded->avc_decoder_ = std::make_unique<PluginAvcDecoder>(table);
    }
    return loaded;
}

std::filesystem::path DefaultCodecPluginFilename()
{
    std::string filename = platform::DynamicLibrary::SharedLibraryPrefix();
    filename += "repsp_codec_ffmpeg";
    filename += platform::DynamicLibrary::SharedLibraryExtension();
    return std::filesystem::path(filename);
}

} // namespace repsp::codec
