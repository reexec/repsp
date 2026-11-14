#include "pixel_convert.h"

#include "repsp/codec/avc_decoder.h"

#include <libavutil/samplefmt.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace repsp::codec_ffmpeg
{

namespace
{

uint8_t ClampU8(int v)
{
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

int16_t FloatToS16(float sample)
{
    if (!std::isfinite(sample))
    {
        return 0;
    }
    const float scaled = std::clamp(sample, -1.0f, 1.0f) * 32767.0f;
    return static_cast<int16_t>(std::lrintf(scaled));
}

void YuvToRgbLimited(uint8_t y, uint8_t u, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b)
{
    const int c = static_cast<int>(y) - 16;
    const int d = static_cast<int>(u) - 128;
    const int e = static_cast<int>(v) - 128;
    r = ClampU8((298 * c + 409 * e + 128) >> 8);
    g = ClampU8((298 * c - 100 * d - 208 * e + 128) >> 8);
    b = ClampU8((298 * c + 516 * d + 128) >> 8);
}

void YuvToRgbFull(uint8_t y, uint8_t u, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b)
{
    const int yi = static_cast<int>(y);
    const int d = static_cast<int>(u) - 128;
    const int e = static_cast<int>(v) - 128;
    r = ClampU8(yi + ((359 * e + 128) >> 8));
    g = ClampU8(yi - ((88 * d + 183 * e - 128) >> 8));
    b = ClampU8(yi + ((454 * d + 128) >> 8));
}

} // namespace

bool ConvertYuv420pToPsp(const uint8_t* y_plane,
                         int y_stride,
                         const uint8_t* u_plane,
                         int u_stride,
                         const uint8_t* v_plane,
                         int v_stride,
                         uint32_t src_width,
                         uint32_t src_height,
                         uint32_t dst_width,
                         uint32_t dst_height,
                         uint32_t pixel_format,
                         bool full_range,
                         uint8_t* out_bytes,
                         size_t out_size)
{
    if (y_plane == nullptr || u_plane == nullptr || v_plane == nullptr ||
        out_bytes == nullptr || dst_width == 0 || dst_height == 0 ||
        src_width == 0 || src_height == 0)
    {
        return false;
    }

    const size_t bpp = (pixel_format == codec::kAvcPixelFormat8888) ? 4u : 2u;
    const size_t required = static_cast<size_t>(dst_width) * static_cast<size_t>(dst_height) * bpp;
    if (out_size < required)
    {
        return false;
    }

    if (pixel_format != codec::kAvcPixelFormat5650 &&
        pixel_format != codec::kAvcPixelFormat5551 &&
        pixel_format != codec::kAvcPixelFormat4444 &&
        pixel_format != codec::kAvcPixelFormat8888)
    {
        return false;
    }

    const uint32_t copy_w = std::min(src_width, dst_width);
    const uint32_t copy_h = std::min(src_height, dst_height);

    if (copy_w < dst_width || copy_h < dst_height)
    {
        std::memset(out_bytes, 0, required);
    }

    for (uint32_t py = 0; py < copy_h; ++py)
    {
        const uint8_t* y_row = y_plane + static_cast<ptrdiff_t>(py) * y_stride;
        const uint8_t* u_row = u_plane + static_cast<ptrdiff_t>(py >> 1) * u_stride;
        const uint8_t* v_row = v_plane + static_cast<ptrdiff_t>(py >> 1) * v_stride;
        uint8_t* dst_row = out_bytes + static_cast<size_t>(py) * dst_width * bpp;

        for (uint32_t px = 0; px < copy_w; ++px)
        {
            const uint8_t y = y_row[px];
            const uint8_t u = u_row[px >> 1];
            const uint8_t v = v_row[px >> 1];
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (full_range)
            {
                YuvToRgbFull(y, u, v, r, g, b);
            }
            else
            {
                YuvToRgbLimited(y, u, v, r, g, b);
            }

            if (pixel_format == codec::kAvcPixelFormat8888)
            {
                const uint32_t rgba = 0xFF000000u |
                                      (static_cast<uint32_t>(b) << 16) |
                                      (static_cast<uint32_t>(g) << 8) |
                                      static_cast<uint32_t>(r);
                std::memcpy(dst_row + static_cast<size_t>(px) * 4u, &rgba, sizeof(rgba));
            }
            else
            {
                uint16_t packed = 0;
                switch (pixel_format)
                {
                case codec::kAvcPixelFormat5650:
                    packed = static_cast<uint16_t>(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));
                    break;
                case codec::kAvcPixelFormat5551:
                    packed = static_cast<uint16_t>(0x8000u | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
                    break;
                case codec::kAvcPixelFormat4444:
                default:
                    packed = static_cast<uint16_t>(0xF000u | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4));
                    break;
                }
                std::memcpy(dst_row + static_cast<size_t>(px) * 2u, &packed, sizeof(packed));
            }
        }
    }

    return true;
}

bool ConvertAudioToS16Interleaved(const uint8_t* const* data,
                                  int sample_format,
                                  int channels,
                                  int nb_samples,
                                  int16_t* pcm_out,
                                  size_t out_samples)
{
    if (data == nullptr || pcm_out == nullptr || channels <= 0 || nb_samples < 0)
    {
        return false;
    }

    const size_t ch_count = static_cast<size_t>(channels);
    const size_t copy_frames = std::min(static_cast<size_t>(nb_samples), out_samples);

    switch (static_cast<AVSampleFormat>(sample_format))
    {
    case AV_SAMPLE_FMT_FLTP:
    {
        for (size_t c = 0; c < ch_count; ++c)
        {
            const auto* src = reinterpret_cast<const float*>(data[c]);
            if (src == nullptr)
            {
                return false;
            }
            for (size_t i = 0; i < copy_frames; ++i)
            {
                pcm_out[i * ch_count + c] = FloatToS16(src[i]);
            }
        }
        break;
    }
    case AV_SAMPLE_FMT_FLT:
    {
        const auto* src = reinterpret_cast<const float*>(data[0]);
        if (src == nullptr)
        {
            return false;
        }
        const size_t total = copy_frames * ch_count;
        for (size_t i = 0; i < total; ++i)
        {
            pcm_out[i] = FloatToS16(src[i]);
        }
        break;
    }
    case AV_SAMPLE_FMT_S16P:
    {
        for (size_t c = 0; c < ch_count; ++c)
        {
            const auto* src = reinterpret_cast<const int16_t*>(data[c]);
            if (src == nullptr)
            {
                return false;
            }
            for (size_t i = 0; i < copy_frames; ++i)
            {
                pcm_out[i * ch_count + c] = src[i];
            }
        }
        break;
    }
    case AV_SAMPLE_FMT_S16:
    {
        const auto* src = reinterpret_cast<const int16_t*>(data[0]);
        if (src == nullptr)
        {
            return false;
        }
        std::memcpy(pcm_out, src, copy_frames * ch_count * sizeof(int16_t));
        break;
    }
    default:
        return false;
    }

    if (copy_frames < out_samples)
    {
        std::fill(pcm_out + copy_frames * ch_count, pcm_out + out_samples * ch_count, int16_t{ 0 });
    }
    return true;
}

} // namespace repsp::codec_ffmpeg
