#ifndef REPSP_CODEC_AVC_DECODER_H
#define REPSP_CODEC_AVC_DECODER_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace repsp::codec
{

// PSP display / sceMpeg pixel formats (matching PSP_DISPLAY_PIXEL_FORMAT_*).
constexpr uint32_t kAvcPixelFormat5650 = 0;
constexpr uint32_t kAvcPixelFormat5551 = 1;
constexpr uint32_t kAvcPixelFormat4444 = 2;
constexpr uint32_t kAvcPixelFormat8888 = 3;

// Replaceable codec backend for H.264/AVC video access units.
class AvcDecoder
{
public:
    virtual ~AvcDecoder() = default;
    virtual bool DecodeAu(std::span<const uint8_t> au_bytes,
                          uint32_t width,
                          uint32_t height,
                          uint32_t pixel_format,
                          std::span<uint8_t> out) = 0;
    virtual void Flush() {}
    virtual const char* Name() const = 0;
};

// Default decoder used when no permissive H.264 codec library is linked:
// fills width * height pixels of the requested pixel format with opaque black.
class BlankAvcDecoder final : public AvcDecoder
{
public:
    bool DecodeAu(std::span<const uint8_t> au_bytes,
                  uint32_t width,
                  uint32_t height,
                  uint32_t pixel_format,
                  std::span<uint8_t> out) override;
    const char* Name() const override { return "blank"; }
};

} // namespace repsp::codec

#endif // REPSP_CODEC_AVC_DECODER_H
