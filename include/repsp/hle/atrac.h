#ifndef REPSP_HLE_ATRAC_H
#define REPSP_HLE_ATRAC_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace repsp::hle::atrac
{

constexpr int32_t kAtracMaxIds = 6;
constexpr uint16_t kAtrac3FormatTag = 0x0270;
constexpr uint16_t kWaveFormatExtensible = 0xFFFE;
constexpr uint32_t kAtrac3SamplesPerFrame = 1024;
constexpr uint32_t kAtrac3PlusSamplesPerFrame = 2048;
constexpr int32_t kAtracRemainAllInMemory = -1;

struct AtracFrameInfo
{
    uint16_t format_tag = 0;
    uint16_t channels = 2;
    uint32_t sample_rate = 44100;
    uint32_t samples_per_frame = kAtrac3PlusSamplesPerFrame;
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

struct AtracStats
{
    uint32_t active_ids = 0;
    uint64_t created_ids = 0;
    uint64_t released_ids = 0;
    uint64_t decode_calls = 0;
    uint64_t decoded_frames = 0;
    uint64_t decoded_samples = 0;
    uint64_t stream_bytes_added = 0;
    uint64_t resets = 0;
    std::string decoder_name = "silent";
};

// Register all 11 sceAtrac3plus NIDs in the runtime NID registry.
void Register();
void Shutdown();

// Override the frame decoder implementation (nullptr restores SilentDecoder).
void SetDecoder(AtracDecoder* decoder);

AtracStats GetStats();
std::string FormatStats();

} // namespace repsp::hle::atrac

#endif // REPSP_HLE_ATRAC_H
