#ifndef REPSP_CODEC_ATRAC_DECODER_H
#define REPSP_CODEC_ATRAC_DECODER_H

#include <cstddef>
#include <cstdint>

namespace repsp::codec
{

struct AtracFrameInfo
{
    uint16_t format_tag = 0;
    uint16_t channels = 2;
    uint32_t sample_rate = 44100;
    uint32_t samples_per_frame = 2048;
    uint32_t block_align = 0;
};

// Replaceable codec backend for ATRAC3 / ATRAC3plus encoded frames.
// PCM output is interleaved s16le with out_samples * info.channels elements.
class AtracDecoder
{
public:
    virtual ~AtracDecoder() = default;
    virtual bool DecodeFrame(const AtracFrameInfo& info,
                             const uint8_t* frame_bytes,
                             size_t frame_size,
                             int16_t* pcm_out,
                             size_t out_samples) = 0;
    virtual const char* Name() const = 0;
};

// Default decoder used when no permissive codec library is available: outputs zeros.
class SilentDecoder final : public AtracDecoder
{
public:
    bool DecodeFrame(const AtracFrameInfo& info,
                     const uint8_t* frame_bytes,
                     size_t frame_size,
                     int16_t* pcm_out,
                     size_t out_samples) override;
    const char* Name() const override { return "silent"; }
};

} // namespace repsp::codec

#endif // REPSP_CODEC_ATRAC_DECODER_H
