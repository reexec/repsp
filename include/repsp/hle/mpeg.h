#ifndef REPSP_HLE_MPEG_H
#define REPSP_HLE_MPEG_H

#include "repsp/codec/atrac_decoder.h"
#include "repsp/codec/avc_decoder.h"

#include <cstdint>
#include <span>
#include <string>

namespace repsp::hle::mpeg
{

constexpr int32_t kMpegPacketSize = 2048;
constexpr int32_t kMpegMemSize = 0x10000;
constexpr int32_t kMpegRingbufferPacketStructSize = 104;
constexpr int32_t kMpegAtracEsSize = 2112;
constexpr int32_t kMpegAtracOutSize = 8192;
constexpr uint32_t kMpegDefaultWidth = 480;
constexpr uint32_t kMpegDefaultHeight = 272;
constexpr uint32_t kMpegDefaultFrameWidth = 512;

constexpr int32_t kMpegStreamAvc = 0;
constexpr int32_t kMpegStreamAtrac = 1;
constexpr int32_t kMpegStreamPcm = 2;
constexpr int32_t kMpegStreamData = 3;
constexpr int32_t kMpegStreamAudio = 15;

struct PsmfStreamEntry
{
    uint8_t stream_id = 0;
    uint8_t substream_id = 0;
    uint8_t stream_num = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct PsmfHeaderInfo
{
    bool valid = false;
    char version[5] = {};
    uint32_t stream_offset = 0;
    uint32_t stream_size = 0;
    uint64_t first_pts = 0;
    uint64_t last_pts = 0;
    uint32_t width = kMpegDefaultWidth;
    uint32_t height = kMpegDefaultHeight;
    uint16_t stream_count = 0;
};

struct MpegStats
{
    uint64_t init_calls = 0;
    uint64_t finish_calls = 0;
    uint64_t create_calls = 0;
    uint64_t delete_calls = 0;
    uint64_t ringbuffer_put_calls = 0;
    uint64_t ringbuffer_packets_added = 0;
    uint64_t psmf_headers_parsed = 0;
    uint64_t streams_registered = 0;
    uint64_t avc_au_calls = 0;
    uint64_t avc_aus_returned = 0;
    uint64_t atrac_au_calls = 0;
    uint64_t atrac_aus_returned = 0;
    uint64_t avc_decode_calls = 0;
    uint64_t avc_frames_decoded = 0;
    uint64_t atrac_decode_calls = 0;
    uint32_t psmf_stream_offset = 0;
    uint32_t psmf_stream_size = 0;
    uint32_t psmf_width = 0;
    uint32_t psmf_height = 0;
    std::string avc_decoder_name = "blank";
    std::string atrac_decoder_name = "silent";
};

// Parse a PSP PSMF header (minimum 0x80 bytes, typically a 2048-byte sector).
bool ParsePsmfHeader(std::span<const uint8_t> data, PsmfHeaderInfo& out);

// Register all 24 sceMpeg NIDs in the runtime NID registry.
void Register();
void Shutdown();

// Override the AVC / ATRAC decoder implementations (nullptr restores defaults).
void SetAvcDecoder(repsp::codec::AvcDecoder* decoder);
void SetAtracDecoder(repsp::codec::AtracDecoder* decoder);

MpegStats GetStats();
std::string FormatStats();

} // namespace repsp::hle::mpeg

#endif // REPSP_HLE_MPEG_H
