#include "repsp/hle/atrac.h"

#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace repsp::hle::atrac
{
namespace
{

constexpr uint32_t kFourCcRiff = 0x46464952u; // "RIFF" LE
constexpr uint32_t kFourCcWave = 0x45564157u; // "WAVE" LE
constexpr uint32_t kFourCcFmt = 0x20746D66u;  // "fmt " LE
constexpr uint32_t kFourCcFact = 0x74636166u; // "fact" LE
constexpr uint32_t kFourCcSmpl = 0x6C706D73u; // "smpl" LE
constexpr uint32_t kFourCcData = 0x61746164u; // "data" LE

// ATRAC3plus WAVEFORMATEXTENSIBLE SubFormat GUID: {BFAA23E9-58CB-4144-A119-FFFA01E4CE62}.
constexpr std::array<uint8_t, 16> kAtrac3PlusSubFormatGuid = {
    0xE9, 0x23, 0xAA, 0xBF, 0xCB, 0x58, 0x44, 0x41,
    0xA1, 0x19, 0xFF, 0xFA, 0x01, 0xE4, 0xCE, 0x62,
};

struct AtracHandle
{
    bool active = false;
    uint32_t buf_addr = 0;
    uint32_t buf_size = 0;

    AtracFrameInfo info{};
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    uint32_t end_sample = 0;
    uint32_t encoder_delay = 0;
    int32_t loop_start = -1;
    int32_t loop_end = -1;

    bool all_in_memory = false;
    bool decoded_all = false;
    int32_t loop_num = 0;
    int32_t last_internal_error = 0;

    uint32_t current_sample = 0;
    uint32_t frame_index = 0;

    // Streaming ring tracking within guest [buf_addr, buf_addr + buf_size).
    uint32_t file_read_offset = 0;
    uint32_t buffered_bytes = 0;
    uint32_t write_cursor = 0;

    void* codec_ctx = nullptr;
    AtracDecoder* codec_ctx_owner = nullptr;
};

struct AtracGlobalState
{
    std::mutex mutex;
    bool registered = false;
    std::array<AtracHandle, kAtracMaxIds> handles{};
    SilentDecoder default_decoder{};
    AtracDecoder* decoder = &default_decoder;

    uint64_t created_ids = 0;
    uint64_t released_ids = 0;
    uint64_t decode_calls = 0;
    uint64_t decoded_frames = 0;
    uint64_t decoded_samples = 0;
    uint64_t stream_bytes_added = 0;
    uint64_t resets = 0;
};

AtracGlobalState g_atrac;

uint16_t ReadLe16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadLe32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

AtracHandle* FindHandle(int32_t id)
{
    if (id < 0 || id >= kAtracMaxIds)
    {
        return nullptr;
    }
    AtracHandle& h = g_atrac.handles[static_cast<size_t>(id)];
    return h.active ? &h : nullptr;
}

void DestroyCodecContext(AtracHandle& h)
{
    if (h.codec_ctx_owner != nullptr && h.codec_ctx != nullptr)
    {
        h.codec_ctx_owner->DestroyStreamContext(h.codec_ctx);
    }
    h.codec_ctx = nullptr;
    h.codec_ctx_owner = nullptr;
}

void EnsureCodecContext(AtracHandle& h, AtracDecoder* dec)
{
    if (h.codec_ctx_owner != dec)
    {
        DestroyCodecContext(h);
        if (dec != nullptr)
        {
            h.codec_ctx_owner = dec;
            h.codec_ctx = dec->CreateStreamContext();
        }
    }
}

int32_t ParseRiffAtrac(const uint8_t* data, uint32_t size, AtracHandle& out)
{
    if (size < 12 || ReadLe32(data) != kFourCcRiff || ReadLe32(data + 8) != kFourCcWave)
    {
        return kSceAtracErrorUnknownFormat;
    }

    bool seen_fmt = false;
    bool seen_fact = false;
    bool seen_data = false;
    uint32_t pos = 12;

    while (pos + 8 <= size)
    {
        const uint32_t chunk_id = ReadLe32(data + pos);
        const uint32_t chunk_size = ReadLe32(data + pos + 4);
        const uint32_t payload_off = pos + 8;

        if (chunk_id == kFourCcData)
        {
            out.data_offset = payload_off;
            out.data_size = chunk_size;
            seen_data = true;
            break;
        }

        if (payload_off > size || chunk_size > size - payload_off)
        {
            return kSceAtracErrorBadData;
        }

        const uint8_t* chunk = data + payload_off;
        if (chunk_id == kFourCcFmt)
        {
            if (chunk_size < 16)
            {
                return kSceAtracErrorBadData;
            }
            const uint16_t fmt_tag = ReadLe16(chunk);
            const uint16_t channels = ReadLe16(chunk + 2);
            const uint32_t sample_rate = ReadLe32(chunk + 4);
            const uint16_t block_align = ReadLe16(chunk + 12);

            if (channels == 0 || channels > 2 || sample_rate == 0 || block_align == 0)
            {
                return kSceAtracErrorBadData;
            }

            out.info.format_tag = fmt_tag;
            out.info.channels = channels;
            out.info.sample_rate = sample_rate;
            out.info.block_align = block_align;
            out.info.extradata.clear();
            if (chunk_size > 18)
            {
                out.info.extradata.assign(chunk + 18, chunk + chunk_size);
            }

            if (fmt_tag == kAtrac3FormatTag)
            {
                out.info.samples_per_frame = kAtrac3SamplesPerFrame;
            }
            else if (fmt_tag == kWaveFormatExtensible)
            {
                if (chunk_size < 40)
                {
                    return kSceAtracErrorBadData;
                }
                if (std::memcmp(chunk + 24,
                                kAtrac3PlusSubFormatGuid.data(),
                                kAtrac3PlusSubFormatGuid.size()) != 0)
                {
                    return kSceAtracErrorUnknownFormat;
                }
                out.info.samples_per_frame = kAtrac3PlusSamplesPerFrame;
            }
            else
            {
                return kSceAtracErrorUnknownFormat;
            }
            seen_fmt = true;
        }
        else if (chunk_id == kFourCcFact)
        {
            if (chunk_size >= 4)
            {
                out.end_sample = ReadLe32(chunk);
                if (chunk_size >= 8)
                {
                    out.encoder_delay = ReadLe32(chunk + 4);
                }
                seen_fact = true;
            }
        }
        else if (chunk_id == kFourCcSmpl)
        {
            if (chunk_size >= 36)
            {
                const uint32_t loop_count = ReadLe32(chunk + 28);
                if (loop_count > 0 && chunk_size >= 36 + 24)
                {
                    out.loop_start = static_cast<int32_t>(ReadLe32(chunk + 36 + 8));
                    out.loop_end = static_cast<int32_t>(ReadLe32(chunk + 36 + 12));
                }
            }
        }

        const uint32_t padded = (chunk_size + 1u) & ~1u;
        if (padded > size - payload_off)
        {
            break;
        }
        pos = payload_off + padded;
    }

    if (!seen_fmt || !seen_data)
    {
        return kSceAtracErrorBadData;
    }

    if (!seen_fact || out.end_sample == 0)
    {
        const uint32_t frame_count =
            (out.data_size + out.info.block_align - 1) / out.info.block_align;
        out.end_sample = frame_count * out.info.samples_per_frame;
    }

    return 0;
}

uint32_t ComputeRemainBytesToRead(const AtracHandle& h)
{
    const uint32_t total_file_bytes = h.data_offset + h.data_size;
    if (h.file_read_offset >= total_file_bytes)
    {
        return 0;
    }
    return total_file_bytes - h.file_read_offset;
}

uint32_t ComputeWritableChunkSize(const AtracHandle& h)
{
    if (h.all_in_memory || h.buffered_bytes >= h.buf_size)
    {
        return 0;
    }
    const uint32_t free_bytes = h.buf_size - h.buffered_bytes;
    const uint32_t contig_to_end = h.buf_size - h.write_cursor;
    const uint32_t remain_in_file = ComputeRemainBytesToRead(h);
    return std::min({ free_bytes, contig_to_end, remain_in_file });
}

int32_t ComputeRemainFrames(const AtracHandle& h)
{
    if (h.all_in_memory)
    {
        return kAtracRemainAllInMemory;
    }
    if (h.info.block_align == 0)
    {
        return 0;
    }
    return static_cast<int32_t>(h.buffered_bytes / h.info.block_align);
}

void HleAtracSetDataAndGetId()
{
    const uint32_t buf = recomp::Arg<uint32_t>(0);
    const uint32_t buf_size = recomp::Arg<uint32_t>(1);

    if (buf == 0 || buf_size < 36)
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }
    if ((buf & 3u) != 0)
    {
        recomp::Return(kSceAtracErrorNot4ByteAlignment);
        return;
    }

    auto& mem = runtime::GetGuestMemory();
    if (!mem.IsValidAddress(buf, buf_size))
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }

    const uint32_t header_scan = std::min<uint32_t>(buf_size, 65536u);
    const uint8_t* raw = mem.Translate(buf);
    if (raw == nullptr)
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }

    AtracHandle parsed{};
    const int32_t parse_rc = ParseRiffAtrac(raw, header_scan, parsed);
    if (parse_rc < 0)
    {
        recomp::Return(parse_rc);
        return;
    }

    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    int32_t free_id = -1;
    for (int32_t i = 0; i < kAtracMaxIds; ++i)
    {
        if (!g_atrac.handles[static_cast<size_t>(i)].active)
        {
            free_id = i;
            break;
        }
    }
    if (free_id < 0)
    {
        recomp::Return(kSceAtracErrorNoAtracId);
        return;
    }

    parsed.active = true;
    parsed.buf_addr = buf;
    parsed.buf_size = buf_size;
    const uint32_t total_file_bytes = parsed.data_offset + parsed.data_size;
    parsed.all_in_memory = (buf_size >= total_file_bytes);
    if (parsed.all_in_memory)
    {
        parsed.file_read_offset = total_file_bytes;
        parsed.buffered_bytes = parsed.data_size;
        parsed.write_cursor = 0;
    }
    else
    {
        parsed.file_read_offset = buf_size;
        parsed.buffered_bytes = (buf_size > parsed.data_offset) ? (buf_size - parsed.data_offset) : 0;
        parsed.write_cursor = 0;
    }

    g_atrac.handles[static_cast<size_t>(free_id)] = parsed;
    g_atrac.created_ids += 1;
    recomp::Return(free_id);
}

void HleAtracDecodeData()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const uint32_t out_addr = recomp::Arg<uint32_t>(1);
    const uint32_t out_samples_addr = recomp::Arg<uint32_t>(2);
    const uint32_t out_end_addr = recomp::Arg<uint32_t>(3);
    const uint32_t out_remain_addr = recomp::Arg<uint32_t>(4);

    auto& mem = runtime::GetGuestMemory();
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }
    if (h->decoded_all)
    {
        if (out_samples_addr != 0 && mem.IsValidAddress(out_samples_addr, 4))
        {
            mem.WriteU32(out_samples_addr, 0);
        }
        if (out_end_addr != 0 && mem.IsValidAddress(out_end_addr, 4))
        {
            mem.WriteU32(out_end_addr, 1);
        }
        if (out_remain_addr != 0 && mem.IsValidAddress(out_remain_addr, 4))
        {
            mem.WriteU32(out_remain_addr, static_cast<uint32_t>(ComputeRemainFrames(*h)));
        }
        recomp::Return(kSceAtracErrorAllDataWasDecoded);
        return;
    }

    const uint32_t spf = h->info.samples_per_frame;
    const size_t max_pcm_elems = static_cast<size_t>(spf) * h->info.channels;
    if (out_addr == 0 || !mem.IsValidAddress(out_addr, max_pcm_elems * sizeof(int16_t)))
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }

    if (!h->all_in_memory && h->buffered_bytes < h->info.block_align &&
        ComputeRemainBytesToRead(*h) > 0)
    {
        if (out_samples_addr != 0 && mem.IsValidAddress(out_samples_addr, 4))
        {
            mem.WriteU32(out_samples_addr, 0);
        }
        if (out_end_addr != 0 && mem.IsValidAddress(out_end_addr, 4))
        {
            mem.WriteU32(out_end_addr, 0);
        }
        if (out_remain_addr != 0 && mem.IsValidAddress(out_remain_addr, 4))
        {
            mem.WriteU32(out_remain_addr, 0);
        }
        recomp::Return(int32_t{ 0 });
        return;
    }

    uint32_t samples_this_frame = spf;
    if (h->current_sample < h->end_sample)
    {
        samples_this_frame = std::min<uint32_t>(spf, h->end_sample - h->current_sample);
    }

    std::vector<uint8_t> encoded_frame(h->info.block_align, 0);
    if (h->all_in_memory)
    {
        const uint32_t byte_off = h->data_offset + h->frame_index * h->info.block_align;
        if (byte_off < h->data_offset + h->data_size && mem.IsValidAddress(h->buf_addr + byte_off, 1))
        {
            const uint32_t avail = std::min<uint32_t>(h->info.block_align,
                                                      (h->data_offset + h->data_size) - byte_off);
            const uint8_t* src = mem.Translate(h->buf_addr + byte_off);
            if (src != nullptr)
            {
                std::memcpy(encoded_frame.data(), src, avail);
            }
        }
    }
    else
    {
        const uint32_t consume = std::min<uint32_t>(h->info.block_align, h->buffered_bytes);
        const uint32_t ring_read =
            (h->write_cursor + h->buf_size - h->buffered_bytes) % h->buf_size;
        for (uint32_t i = 0; i < consume; ++i)
        {
            const uint32_t pos = (ring_read + i) % h->buf_size;
            encoded_frame[i] = mem.ReadU8(h->buf_addr + pos);
        }
        h->buffered_bytes -= consume;
    }

    std::vector<int16_t> pcm_tmp(static_cast<size_t>(samples_this_frame) * h->info.channels, 0);
    AtracDecoder* dec = (g_atrac.decoder != nullptr) ? g_atrac.decoder : &g_atrac.default_decoder;
    EnsureCodecContext(*h, dec);
    if (!dec->DecodeFrame(h->codec_ctx,
                          h->info,
                          encoded_frame.data(),
                          encoded_frame.size(),
                          pcm_tmp.data(),
                          samples_this_frame))
    {
        recomp::Return(kSceAtracErrorBadData);
        return;
    }

    uint8_t* dst = mem.Translate(out_addr);
    if (dst == nullptr)
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }
    std::memcpy(dst, pcm_tmp.data(), pcm_tmp.size() * sizeof(int16_t));

    h->current_sample += samples_this_frame;
    h->frame_index += 1;

    uint32_t ended = 0;
    const uint32_t loop_limit =
        (h->loop_end > 0 && h->loop_start >= 0 && h->loop_num != 0)
            ? static_cast<uint32_t>(h->loop_end)
            : h->end_sample;

    if (h->current_sample >= loop_limit)
    {
        if (h->loop_num != 0)
        {
            if (h->loop_num > 0)
            {
                h->loop_num -= 1;
            }
            const uint32_t target = (h->loop_start >= 0) ? static_cast<uint32_t>(h->loop_start) : 0u;
            h->current_sample = target;
            h->frame_index = target / spf;
            if (h->codec_ctx_owner != nullptr && h->codec_ctx != nullptr)
            {
                h->codec_ctx_owner->ResetStreamContext(h->codec_ctx);
            }
            if (!h->all_in_memory)
            {
                h->file_read_offset = h->data_offset + h->frame_index * h->info.block_align;
                if (h->loop_start < 0)
                {
                    h->buffered_bytes = 0;
                    h->write_cursor = 0;
                }
            }
        }
        else
        {
            ended = 1;
            h->decoded_all = true;
        }
    }

    if (out_samples_addr != 0 && mem.IsValidAddress(out_samples_addr, 4))
    {
        mem.WriteU32(out_samples_addr, samples_this_frame);
    }
    if (out_end_addr != 0 && mem.IsValidAddress(out_end_addr, 4))
    {
        mem.WriteU32(out_end_addr, ended);
    }
    if (out_remain_addr != 0 && mem.IsValidAddress(out_remain_addr, 4))
    {
        mem.WriteU32(out_remain_addr, static_cast<uint32_t>(ComputeRemainFrames(*h)));
    }

    g_atrac.decode_calls += 1;
    g_atrac.decoded_frames += 1;
    g_atrac.decoded_samples += samples_this_frame;
    recomp::Return(int32_t{ 0 });
}

void HleAtracGetRemainFrame()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const uint32_t out_remain_addr = recomp::Arg<uint32_t>(1);

    auto& mem = runtime::GetGuestMemory();
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }
    if (out_remain_addr == 0 || !mem.IsValidAddress(out_remain_addr, 4))
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }

    mem.WriteU32(out_remain_addr, static_cast<uint32_t>(ComputeRemainFrames(*h)));
    recomp::Return(int32_t{ 0 });
}

void HleAtracGetStreamDataInfo()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const uint32_t write_ptr_addr = recomp::Arg<uint32_t>(1);
    const uint32_t avail_bytes_addr = recomp::Arg<uint32_t>(2);
    const uint32_t read_offset_addr = recomp::Arg<uint32_t>(3);

    auto& mem = runtime::GetGuestMemory();
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }

    const uint32_t avail = ComputeWritableChunkSize(*h);
    const uint32_t write_ptr = (avail > 0) ? (h->buf_addr + h->write_cursor) : 0u;
    const uint32_t read_off = (avail > 0) ? h->file_read_offset : 0u;

    if (write_ptr_addr != 0 && mem.IsValidAddress(write_ptr_addr, 4))
    {
        mem.WriteU32(write_ptr_addr, write_ptr);
    }
    if (avail_bytes_addr != 0 && mem.IsValidAddress(avail_bytes_addr, 4))
    {
        mem.WriteU32(avail_bytes_addr, avail);
    }
    if (read_offset_addr != 0 && mem.IsValidAddress(read_offset_addr, 4))
    {
        mem.WriteU32(read_offset_addr, read_off);
    }

    recomp::Return(int32_t{ 0 });
}

void HleAtracAddStreamData()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const uint32_t bytes_to_add = recomp::Arg<uint32_t>(1);

    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }
    if (h->all_in_memory)
    {
        recomp::Return(bytes_to_add == 0 ? 0 : kSceAtracErrorAllDataIsOnMemory);
        return;
    }

    const uint32_t max_add = ComputeWritableChunkSize(*h);
    if (bytes_to_add > max_add)
    {
        recomp::Return(kSceAtracErrorAddDataIsTooBig);
        return;
    }

    h->buffered_bytes += bytes_to_add;
    h->file_read_offset += bytes_to_add;
    h->write_cursor = (h->write_cursor + bytes_to_add) % h->buf_size;
    g_atrac.stream_bytes_added += bytes_to_add;
    recomp::Return(int32_t{ 0 });
}

void HleAtracSetLoopNum()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const int32_t loop_num = recomp::Arg<int32_t>(1);

    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }
    if (loop_num < -1)
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }

    h->loop_num = loop_num;
    recomp::Return(int32_t{ 0 });
}

void HleAtracReleaseId()
{
    const int32_t id = recomp::Arg<int32_t>(0);

    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }

    DestroyCodecContext(*h);
    *h = AtracHandle{};
    g_atrac.released_ids += 1;
    recomp::Return(int32_t{ 0 });
}

void HleAtracGetSoundSample()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const uint32_t end_sample_addr = recomp::Arg<uint32_t>(1);
    const uint32_t loop_start_addr = recomp::Arg<uint32_t>(2);
    const uint32_t loop_end_addr = recomp::Arg<uint32_t>(3);

    auto& mem = runtime::GetGuestMemory();
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }

    if (end_sample_addr != 0 && mem.IsValidAddress(end_sample_addr, 4))
    {
        mem.WriteU32(end_sample_addr, h->end_sample);
    }
    if (loop_start_addr != 0 && mem.IsValidAddress(loop_start_addr, 4))
    {
        mem.WriteU32(loop_start_addr, static_cast<uint32_t>(h->loop_start));
    }
    if (loop_end_addr != 0 && mem.IsValidAddress(loop_end_addr, 4))
    {
        mem.WriteU32(loop_end_addr, static_cast<uint32_t>(h->loop_end));
    }

    recomp::Return(int32_t{ 0 });
}

void HleAtracGetBufferInfoForReseting()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const uint32_t sample = recomp::Arg<uint32_t>(1);
    const uint32_t info_addr = recomp::Arg<uint32_t>(2);

    auto& mem = runtime::GetGuestMemory();
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }
    if (sample > h->end_sample)
    {
        recomp::Return(kSceAtracErrorBadSample);
        return;
    }
    if (info_addr == 0 || !mem.IsValidAddress(info_addr, 32))
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }

    // PspBufferInfo layout (32 bytes):
    // +0: pucWriteDataFirstBuf, +4: uiWritableByteFirstBuf, +8: uiMinWriteByteFirstBuf,
    // +12: uiReadPositionFirstBuf, +16..+28: second buffer fields.
    const uint32_t target_frame = sample / h->info.samples_per_frame;
    const uint32_t target_file_off =
        std::min<uint32_t>(h->data_offset + target_frame * h->info.block_align,
                           h->data_offset + h->data_size);
    const uint32_t remain_bytes = (h->data_offset + h->data_size) - target_file_off;

    uint32_t write_ptr_1 = 0;
    uint32_t write_bytes_1 = 0;
    uint32_t min_write_1 = 0;
    if (!h->all_in_memory)
    {
        write_ptr_1 = h->buf_addr;
        write_bytes_1 = std::min<uint32_t>(h->buf_size, remain_bytes);
        min_write_1 = std::min<uint32_t>(write_bytes_1, h->info.block_align);
    }

    mem.WriteU32(info_addr + 0, write_ptr_1);
    mem.WriteU32(info_addr + 4, write_bytes_1);
    mem.WriteU32(info_addr + 8, min_write_1);
    mem.WriteU32(info_addr + 12, target_file_off);
    mem.WriteU32(info_addr + 16, 0);
    mem.WriteU32(info_addr + 20, 0);
    mem.WriteU32(info_addr + 24, 0);
    mem.WriteU32(info_addr + 28, target_file_off);

    recomp::Return(int32_t{ 0 });
}

void HleAtracResetPlayPosition()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const uint32_t sample = recomp::Arg<uint32_t>(1);
    const uint32_t write_byte_first = recomp::Arg<uint32_t>(2);
    const uint32_t write_byte_second = recomp::Arg<uint32_t>(3);
    (void)write_byte_second;

    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }
    if (sample > h->end_sample)
    {
        recomp::Return(kSceAtracErrorBadSample);
        return;
    }
    if (!h->all_in_memory && write_byte_first > h->buf_size)
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }

    h->current_sample = sample;
    h->frame_index = sample / h->info.samples_per_frame;
    h->decoded_all = false;
    if (h->codec_ctx_owner != nullptr && h->codec_ctx != nullptr)
    {
        h->codec_ctx_owner->ResetStreamContext(h->codec_ctx);
    }
    if (!h->all_in_memory)
    {
        const uint32_t base_file_off =
            std::min<uint32_t>(h->data_offset + h->frame_index * h->info.block_align,
                               h->data_offset + h->data_size);
        h->buffered_bytes = write_byte_first;
        h->file_read_offset = base_file_off + write_byte_first;
        h->write_cursor = write_byte_first % h->buf_size;
    }

    g_atrac.resets += 1;
    recomp::Return(int32_t{ 0 });
}

void HleAtracGetInternalErrorInfo()
{
    const int32_t id = recomp::Arg<int32_t>(0);
    const uint32_t out_result_addr = recomp::Arg<uint32_t>(1);

    auto& mem = runtime::GetGuestMemory();
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracHandle* h = FindHandle(id);
    if (h == nullptr)
    {
        recomp::Return(kSceAtracErrorBadAtracId);
        return;
    }
    if (out_result_addr == 0 || !mem.IsValidAddress(out_result_addr, 4))
    {
        recomp::Return(kSceAtracErrorParamFail);
        return;
    }

    mem.WriteU32(out_result_addr, static_cast<uint32_t>(h->last_internal_error));
    recomp::Return(int32_t{ 0 });
}

} // namespace

void Register()
{
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    for (auto& h : g_atrac.handles)
    {
        DestroyCodecContext(h);
        h = AtracHandle{};
    }
    g_atrac.created_ids = 0;
    g_atrac.released_ids = 0;
    g_atrac.decode_calls = 0;
    g_atrac.decoded_frames = 0;
    g_atrac.decoded_samples = 0;
    g_atrac.stream_bytes_added = 0;
    g_atrac.resets = 0;
    g_atrac.registered = true;

    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceAtrac3plus", "sceAtracSetDataAndGetID", HleAtracSetDataAndGetId);
    reg.RegisterByName("sceAtrac3plus", "sceAtracDecodeData", HleAtracDecodeData);
    reg.RegisterByName("sceAtrac3plus", "sceAtracGetRemainFrame", HleAtracGetRemainFrame);
    reg.RegisterByName("sceAtrac3plus", "sceAtracGetStreamDataInfo", HleAtracGetStreamDataInfo);
    reg.RegisterByName("sceAtrac3plus", "sceAtracAddStreamData", HleAtracAddStreamData);
    reg.RegisterByName("sceAtrac3plus", "sceAtracSetLoopNum", HleAtracSetLoopNum);
    reg.RegisterByName("sceAtrac3plus", "sceAtracReleaseAtracID", HleAtracReleaseId);
    reg.RegisterByName("sceAtrac3plus", "sceAtracGetSoundSample", HleAtracGetSoundSample);
    reg.RegisterByName("sceAtrac3plus", "sceAtracResetPlayPosition", HleAtracResetPlayPosition);
    reg.RegisterByName(
        "sceAtrac3plus", "sceAtracGetBufferInfoForReseting", HleAtracGetBufferInfoForReseting);
    reg.RegisterByName("sceAtrac3plus", "sceAtracGetInternalErrorInfo", HleAtracGetInternalErrorInfo);
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    for (auto& h : g_atrac.handles)
    {
        DestroyCodecContext(h);
        h = AtracHandle{};
    }
    g_atrac.decoder = &g_atrac.default_decoder;
    g_atrac.registered = false;
}

void SetDecoder(AtracDecoder* decoder)
{
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    for (auto& h : g_atrac.handles)
    {
        DestroyCodecContext(h);
    }
    g_atrac.decoder = (decoder != nullptr) ? decoder : &g_atrac.default_decoder;
}

AtracStats GetStats()
{
    std::lock_guard<std::mutex> lock(g_atrac.mutex);
    AtracStats s{};
    for (const auto& h : g_atrac.handles)
    {
        if (h.active)
        {
            s.active_ids += 1;
        }
    }
    s.created_ids = g_atrac.created_ids;
    s.released_ids = g_atrac.released_ids;
    s.decode_calls = g_atrac.decode_calls;
    s.decoded_frames = g_atrac.decoded_frames;
    s.decoded_samples = g_atrac.decoded_samples;
    s.stream_bytes_added = g_atrac.stream_bytes_added;
    s.resets = g_atrac.resets;
    AtracDecoder* dec = (g_atrac.decoder != nullptr) ? g_atrac.decoder : &g_atrac.default_decoder;
    s.decoder_name = dec->Name();
    return s;
}

std::string FormatStats()
{
    const AtracStats s = GetStats();
    char buf[256];
    std::snprintf(buf,
                  sizeof(buf),
                  "ATRAC decode: %s | active=%u created=%llu released=%llu frames=%llu "
                  "samples=%llu streamAdded=%llu resets=%llu",
                  s.decoder_name.c_str(),
                  s.active_ids,
                  static_cast<unsigned long long>(s.created_ids),
                  static_cast<unsigned long long>(s.released_ids),
                  static_cast<unsigned long long>(s.decoded_frames),
                  static_cast<unsigned long long>(s.decoded_samples),
                  static_cast<unsigned long long>(s.stream_bytes_added),
                  static_cast<unsigned long long>(s.resets));
    return std::string(buf);
}

} // namespace repsp::hle::atrac
