#include "repsp/codec/plugin.h"

namespace
{

const RepspCodecPluginV1 kBadAbiTable = {
    999u,
    static_cast<uint32_t>(sizeof(RepspCodecPluginV1)),
    "bad-abi",
    "Fake plugin with mismatched ABI version",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

} // namespace

extern "C" REPSP_CODEC_PLUGIN_EXPORT const RepspCodecPluginV1* repsp_codec_plugin_get_v1(void)
{
    return &kBadAbiTable;
}
