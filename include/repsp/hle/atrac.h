#ifndef REPSP_HLE_ATRAC_H
#define REPSP_HLE_ATRAC_H

#include "repsp/codec/atrac_decoder.h"

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

using AtracFrameInfo = repsp::codec::AtracFrameInfo;
using AtracDecoder = repsp::codec::AtracDecoder;
using SilentDecoder = repsp::codec::SilentDecoder;

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
