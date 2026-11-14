#include "atrac_ffmpeg.h"
#include "avc_ffmpeg.h"

#include "repsp/codec/plugin.h"

extern "C" {
#include <libavcodec/version.h>
}

#define REPSP_STRINGIFY_INNER(x) #x
#define REPSP_STRINGIFY(x) REPSP_STRINGIFY_INNER(x)

namespace
{

const RepspCodecPluginV1 kPluginTable = {
    REPSP_CODEC_PLUGIN_ABI_VERSION,
    static_cast<uint32_t>(sizeof(RepspCodecPluginV1)),
    "ffmpeg",
    "FFmpeg libavcodec " REPSP_STRINGIFY(LIBAVCODEC_VERSION_MAJOR) " (LGPL, dynamic)",
    &repsp::codec_ffmpeg::AtracCreate,
    &repsp::codec_ffmpeg::AtracDestroy,
    &repsp::codec_ffmpeg::AtracReset,
    &repsp::codec_ffmpeg::AtracDecode,
    &repsp::codec_ffmpeg::AvcCreate,
    &repsp::codec_ffmpeg::AvcDestroy,
    &repsp::codec_ffmpeg::AvcFlush,
    &repsp::codec_ffmpeg::AvcDecode,
};

} // namespace

extern "C" REPSP_CODEC_PLUGIN_EXPORT const RepspCodecPluginV1* repsp_codec_plugin_get_v1(void)
{
    return &kPluginTable;
}
