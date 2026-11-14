#include "repsp/codec/atrac_decoder.h"
#include "repsp/codec/avc_decoder.h"

#include <algorithm>
#include <cstring>

namespace repsp::codec
{

bool SilentDecoder::DecodeFrame(const AtracFrameInfo& info,
                                const uint8_t* frame_bytes,
                                size_t frame_size,
                                int16_t* pcm_out,
                                size_t out_samples)
{
    (void)frame_bytes;
    (void)frame_size;
    if (pcm_out == nullptr)
    {
        return false;
    }
    const size_t elem_count = out_samples * info.channels;
    std::fill(pcm_out, pcm_out + elem_count, int16_t{ 0 });
    return true;
}

bool BlankAvcDecoder::DecodeAu(std::span<const uint8_t> au_bytes,
                               uint32_t width,
                               uint32_t height,
                               uint32_t pixel_format,
                               std::span<uint8_t> out)
{
    (void)au_bytes;
    if (width == 0 || height == 0)
    {
        return false;
    }

    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixel_format == kAvcPixelFormat8888)
    {
        const size_t required = pixel_count * 4u;
        if (out.size() < required)
        {
            return false;
        }
        for (size_t i = 0; i < pixel_count; ++i)
        {
            const uint32_t black = 0xFF000000u;
            std::memcpy(out.data() + i * 4u, &black, sizeof(black));
        }
        return true;
    }

    const size_t required = pixel_count * 2u;
    if (out.size() < required)
    {
        return false;
    }

    uint16_t black16 = 0x0000u;
    switch (pixel_format)
    {
    case kAvcPixelFormat5650:
        black16 = 0x0000u;
        break;
    case kAvcPixelFormat5551:
        black16 = 0x8000u;
        break;
    case kAvcPixelFormat4444:
        black16 = 0xF000u;
        break;
    default:
        return false;
    }

    for (size_t i = 0; i < pixel_count; ++i)
    {
        std::memcpy(out.data() + i * 2u, &black16, sizeof(black16));
    }
    return true;
}

} // namespace repsp::codec
