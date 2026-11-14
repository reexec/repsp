#include "repsp/hle/mpeg.h"

#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace repsp::hle::mpeg
{
namespace
{

constexpr uint64_t kInvalidTimestamp = 0xFFFFFFFFFFFFFFFFull;

struct GuestRingbuffer
{
    int32_t packets = 0;
    int32_t read_packets = 0;
    int32_t write_packets = 0;
    int32_t free_packets = 0;
    int32_t packet_size = kMpegPacketSize;
    uint32_t data_addr = 0;
    uint32_t callback_addr = 0;
    uint32_t callback_data = 0;
    uint32_t data_upper_bound = 0;
    int32_t sema_id = -1;
    uint32_t mpeg_addr = 0;
};
static_assert(sizeof(GuestRingbuffer) == 44, "SceMpegRingbuffer must be 44 bytes");

struct GuestAu
{
    uint32_t pts_msb = 0xFFFFFFFFu;
    uint32_t pts_lsb = 0xFFFFFFFFu;
    uint32_t dts_msb = 0xFFFFFFFFu;
    uint32_t dts_lsb = 0xFFFFFFFFu;
    uint32_t es_buffer = 0;
    uint32_t es_size = 0;
};
static_assert(sizeof(GuestAu) == 24, "SceMpegAu must be 24 bytes");

struct AccessUnit
{
    uint64_t pts = kInvalidTimestamp;
    uint64_t dts = kInvalidTimestamp;
    std::vector<uint8_t> data;
    uint32_t source_bytes = 0;
};

enum class StreamEsKind
{
    kFallbackPesPts = 0,
    kAvcAnnexB = 1,
    kPspAtrac3Plus = 2,
};

struct PesTimestampAnchor
{
    uint64_t pes_start = 0;
    uint64_t pes_end = 0;
    uint64_t pts = kInvalidTimestamp;
    uint64_t dts = kInvalidTimestamp;
};

struct PesSourceChunk
{
    uint64_t es_end = 0;
    uint32_t source_bytes = 0;
};

struct PendingAuBuffer
{
    bool active = false;
    bool has_pts = false;
    uint64_t pts = kInvalidTimestamp;
    uint64_t dts = kInvalidTimestamp;
    std::vector<uint8_t> data;
    uint32_t source_bytes = 0;
};

struct RegisteredStream
{
    uint32_t uid = 0;
    int32_t stream_type = 0;
    int32_t stream_num = 0;
    bool active = true;
};

struct StreamQueue
{
    StreamEsKind kind = StreamEsKind::kFallbackPesPts;
    bool es_framing_decided = false;
    PendingAuBuffer pending;
    std::deque<AccessUnit> ready;
    uint64_t es_read_pos = 0;
    uint64_t scan_pos = 0;
    std::vector<uint8_t> es_buf;
    std::deque<PesTimestampAnchor> anchors;
    std::deque<PesSourceChunk> source_chunks;
    uint64_t next_pts = kInvalidTimestamp;
    uint64_t next_dts = kInvalidTimestamp;
};

struct RingbufferState
{
    uint32_t rb_addr = 0;
    int32_t total_packets = 0;
    uint32_t data_addr = 0;
    uint32_t callback_addr = 0;
    uint32_t callback_data = 0;
    uint32_t linked_mpeg_addr = 0;
    int32_t total_packets_read = 0;
    uint32_t buffered_source_bytes = 0;
};

struct MpegInstance
{
    uint32_t mpeg_ptr_addr = 0;
    uint32_t mpeg_data_addr = 0;
    uint32_t rb_addr = 0;
    uint32_t frame_width = kMpegDefaultFrameWidth;
    uint32_t video_width = kMpegDefaultWidth;
    uint32_t video_height = kMpegDefaultHeight;
    uint32_t pixel_format = repsp::codec::kAvcPixelFormat8888;
    bool hit_eof = false;

    uint32_t next_stream_uid = 1;
    uint32_t next_es_handle = 1;
    std::vector<RegisteredStream> streams;
    std::vector<uint32_t> es_handles;

    std::unordered_map<int32_t, StreamQueue> avc_queues;
    std::unordered_map<int32_t, StreamQueue> atrac_queues;
    std::vector<uint8_t> demux_scratch;

    std::unordered_map<uint32_t, AccessUnit> last_avc_aus;
    AccessUnit last_avc_au;
    AccessUnit last_atrac_au;
};

struct MpegGlobalState
{
    std::mutex mutex;
    bool registered = false;
    bool initialized = false;

    repsp::codec::BlankAvcDecoder default_avc_decoder{};
    repsp::codec::SilentDecoder default_atrac_decoder{};
    repsp::codec::AvcDecoder* avc_decoder = &default_avc_decoder;
    repsp::codec::AtracDecoder* atrac_decoder = &default_atrac_decoder;

    std::vector<std::shared_ptr<MpegInstance>> instances;
    std::vector<std::shared_ptr<RingbufferState>> ringbuffers;
    PsmfHeaderInfo last_psmf_info{};

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
};

MpegGlobalState g_mpeg;

uint16_t ReadBe16(const uint8_t* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                 static_cast<uint16_t>(p[1]));
}

uint32_t ReadBe32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

uint64_t ReadBe48(const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 6; ++i)
    {
        v = (v << 8) | static_cast<uint64_t>(p[i]);
    }
    return v;
}

uint64_t ParsePesTimestamp(const uint8_t* p)
{
    const uint64_t bits_32_30 = static_cast<uint64_t>((p[0] >> 1) & 0x07u);
    const uint64_t bits_29_22 = static_cast<uint64_t>(p[1]);
    const uint64_t bits_21_15 = static_cast<uint64_t>((p[2] >> 1) & 0x7Fu);
    const uint64_t bits_14_07 = static_cast<uint64_t>(p[3]);
    const uint64_t bits_06_00 = static_cast<uint64_t>((p[4] >> 1) & 0x7Fu);
    return (bits_32_30 << 30) |
           (bits_29_22 << 22) |
           (bits_21_15 << 15) |
           (bits_14_07 << 7) |
           bits_06_00;
}

bool IsValidPsmfVersion(const uint8_t* p)
{
    return p[0] == '0' && p[1] == '0' && p[2] == '1' &&
           (p[3] == '2' || p[3] == '3' || p[3] == '4' || p[3] == '5');
}

bool ReadGuestRingbuffer(const runtime::GuestMemory& mem, uint32_t addr, GuestRingbuffer& rb)
{
    if (addr == 0 || !mem.IsValidAddress(addr, sizeof(GuestRingbuffer)))
    {
        return false;
    }
    mem.ReadBlock(addr, &rb, sizeof(GuestRingbuffer));
    return true;
}

bool WriteGuestRingbuffer(runtime::GuestMemory& mem, uint32_t addr, const GuestRingbuffer& rb)
{
    if (addr == 0 || !mem.IsValidAddress(addr, sizeof(GuestRingbuffer)))
    {
        return false;
    }
    mem.WriteBlock(addr, &rb, sizeof(GuestRingbuffer));
    return true;
}

bool ReadGuestAu(const runtime::GuestMemory& mem, uint32_t addr, GuestAu& au)
{
    if (addr == 0 || !mem.IsValidAddress(addr, sizeof(GuestAu)))
    {
        return false;
    }
    mem.ReadBlock(addr, &au, sizeof(GuestAu));
    return true;
}

bool WriteGuestAu(runtime::GuestMemory& mem, uint32_t addr, const GuestAu& au)
{
    if (addr == 0 || !mem.IsValidAddress(addr, sizeof(GuestAu)))
    {
        return false;
    }
    mem.WriteBlock(addr, &au, sizeof(GuestAu));
    return true;
}

std::shared_ptr<MpegInstance> FindInstanceLocked(uint32_t mpeg_addr)
{
    if (mpeg_addr == 0)
    {
        return nullptr;
    }
    for (const auto& inst : g_mpeg.instances)
    {
        if (inst->mpeg_ptr_addr == mpeg_addr || inst->mpeg_data_addr == mpeg_addr)
        {
            return inst;
        }
    }
    return nullptr;
}

std::shared_ptr<RingbufferState> FindRingbufferLocked(uint32_t rb_addr)
{
    if (rb_addr == 0)
    {
        return nullptr;
    }
    for (const auto& rb : g_mpeg.ringbuffers)
    {
        if (rb->rb_addr == rb_addr)
        {
            return rb;
        }
    }
    return nullptr;
}

std::shared_ptr<MpegInstance> FindInstanceByRingbufferLocked(uint32_t rb_addr)
{
    if (auto rb = FindRingbufferLocked(rb_addr); rb && rb->linked_mpeg_addr != 0)
    {
        if (auto inst = FindInstanceLocked(rb->linked_mpeg_addr))
        {
            return inst;
        }
    }
    for (const auto& inst : g_mpeg.instances)
    {
        if (inst->rb_addr == rb_addr)
        {
            return inst;
        }
    }
    if (!g_mpeg.instances.empty())
    {
        return g_mpeg.instances.front();
    }
    return nullptr;
}

constexpr uint64_t kAvcFramePtsStep = 3003ull;
constexpr uint64_t kAtracFramePtsStep = 4180ull;

size_t FindH264AudOffset(const uint8_t* data, size_t size, size_t start_offset)
{
    if (size < 5u || start_offset + 5u > size)
    {
        return size;
    }
    for (size_t i = start_offset; i + 5u <= size; ++i)
    {
        if (data[i] == 0x00u &&
            data[i + 1u] == 0x00u &&
            data[i + 2u] == 0x00u &&
            data[i + 3u] == 0x01u &&
            data[i + 4u] == 0x09u)
        {
            return i;
        }
    }
    return size;
}

size_t ParsePspAtracAuSize(const uint8_t* hdr)
{
    const size_t body_bytes =
        (((static_cast<size_t>(hdr[2]) & 0x03u) << 8) | static_cast<size_t>(hdr[3])) * 8u + 8u;
    return 8u + body_bytes;
}

uint32_t ConsumeSourceBytesUpToLocked(StreamQueue& q, uint64_t consumed_end, bool drain_all)
{
    uint32_t total = 0;
    while (!q.source_chunks.empty() &&
           (drain_all || q.source_chunks.front().es_end <= consumed_end))
    {
        total += q.source_chunks.front().source_bytes;
        q.source_chunks.pop_front();
    }
    return total;
}

void EmitAvcAuSliceLocked(StreamQueue& q, size_t au_local_end, size_t first_aud_local, bool drain_all_chunks)
{
    const uint64_t au_start = q.es_read_pos;
    const uint64_t aud_end_pos = q.es_read_pos + static_cast<uint64_t>(first_aud_local) + 5u;

    while (!q.anchors.empty() && q.anchors.front().pes_end <= au_start)
    {
        q.anchors.pop_front();
    }

    uint64_t pts = q.next_pts;
    uint64_t dts = q.next_dts;
    if (!q.anchors.empty() &&
        q.anchors.front().pes_start < aud_end_pos &&
        au_start < q.anchors.front().pes_end)
    {
        pts = q.anchors.front().pts;
        dts = q.anchors.front().dts;
        q.anchors.pop_front();
    }

    if (pts != kInvalidTimestamp)
    {
        q.next_pts = pts + kAvcFramePtsStep;
        q.next_dts = (dts != kInvalidTimestamp) ? (dts + kAvcFramePtsStep) : q.next_pts;

        AccessUnit au;
        au.pts = pts;
        au.dts = (dts != kInvalidTimestamp) ? dts : pts;
        au.data.assign(q.es_buf.begin(), q.es_buf.begin() + static_cast<ptrdiff_t>(au_local_end));
        au.source_bytes = ConsumeSourceBytesUpToLocked(q, q.es_read_pos + au_local_end, drain_all_chunks);
        q.ready.push_back(std::move(au));
    }
    else
    {
        (void)ConsumeSourceBytesUpToLocked(q, q.es_read_pos + au_local_end, drain_all_chunks);
    }

    q.es_buf.erase(q.es_buf.begin(), q.es_buf.begin() + static_cast<ptrdiff_t>(au_local_end));
    q.es_read_pos += au_local_end;
}

void DrainAvcAusLocked(StreamQueue& q, bool force_eof)
{
    while (!q.es_buf.empty())
    {
        const size_t first_aud = FindH264AudOffset(q.es_buf.data(), q.es_buf.size(), 0u);
        if (first_aud == q.es_buf.size())
        {
            break;
        }

        const size_t min_next = first_aud + 5u;
        size_t search_local = min_next;
        if (q.scan_pos > q.es_read_pos)
        {
            search_local = std::max<size_t>(min_next, static_cast<size_t>(q.scan_pos - q.es_read_pos));
        }

        const size_t next_aud = (search_local < q.es_buf.size())
                                    ? FindH264AudOffset(q.es_buf.data(), q.es_buf.size(), search_local)
                                    : q.es_buf.size();
        if (next_aud >= q.es_buf.size())
        {
            break;
        }

        EmitAvcAuSliceLocked(q, next_aud, first_aud, false);
        q.scan_pos = q.es_read_pos + 5u;
    }

    if (force_eof && !q.es_buf.empty())
    {
        const size_t first_aud = FindH264AudOffset(q.es_buf.data(), q.es_buf.size(), 0u);
        if (first_aud < q.es_buf.size())
        {
            EmitAvcAuSliceLocked(q, q.es_buf.size(), first_aud, true);
        }
        else
        {
            q.es_buf.clear();
            q.anchors.clear();
            q.source_chunks.clear();
        }
    }

    q.scan_pos = (q.es_buf.size() >= 4u)
                     ? (q.es_read_pos + static_cast<uint64_t>(q.es_buf.size() - 4u))
                     : q.es_read_pos;
    q.pending.active = !q.es_buf.empty();
}

void DrainAtracAusLocked(StreamQueue& q, bool force_eof)
{
    while (q.es_buf.size() >= 8u)
    {
        if (!(q.es_buf[0] == 0x0Fu && q.es_buf[1] == 0xD0u))
        {
            size_t sync_pos = 1u;
            while (sync_pos + 2u <= q.es_buf.size() &&
                   !(q.es_buf[sync_pos] == 0x0Fu && q.es_buf[sync_pos + 1u] == 0xD0u))
            {
                ++sync_pos;
            }
            const size_t erase_bytes = (sync_pos + 2u <= q.es_buf.size()) ? sync_pos : (q.es_buf.size() - 1u);
            (void)ConsumeSourceBytesUpToLocked(q, q.es_read_pos + erase_bytes, false);
            q.es_buf.erase(q.es_buf.begin(), q.es_buf.begin() + static_cast<ptrdiff_t>(erase_bytes));
            q.es_read_pos += erase_bytes;
            if (q.es_buf.size() < 8u || !(q.es_buf[0] == 0x0Fu && q.es_buf[1] == 0xD0u))
            {
                break;
            }
        }

        const size_t frame_bytes = ParsePspAtracAuSize(q.es_buf.data());
        if (q.es_buf.size() < frame_bytes)
        {
            break;
        }

        const uint64_t au_start = q.es_read_pos;
        while (!q.anchors.empty() && q.anchors.front().pes_start < au_start)
        {
            q.anchors.pop_front();
        }

        uint64_t pts = q.next_pts;
        uint64_t dts = q.next_dts;
        if (!q.anchors.empty() && q.anchors.front().pes_start == au_start)
        {
            pts = q.anchors.front().pts;
            dts = q.anchors.front().dts;
            q.anchors.pop_front();
        }

        if (pts != kInvalidTimestamp)
        {
            q.next_pts = pts + kAtracFramePtsStep;
            q.next_dts = (dts != kInvalidTimestamp) ? (dts + kAtracFramePtsStep) : q.next_pts;

            AccessUnit au;
            au.pts = pts;
            au.dts = (dts != kInvalidTimestamp) ? dts : pts;
            au.data.assign(q.es_buf.begin(), q.es_buf.begin() + static_cast<ptrdiff_t>(frame_bytes));
            au.source_bytes = ConsumeSourceBytesUpToLocked(q, q.es_read_pos + frame_bytes, false);
            q.ready.push_back(std::move(au));
        }
        else
        {
            (void)ConsumeSourceBytesUpToLocked(q, q.es_read_pos + frame_bytes, false);
        }

        q.es_buf.erase(q.es_buf.begin(), q.es_buf.begin() + static_cast<ptrdiff_t>(frame_bytes));
        q.es_read_pos += frame_bytes;
    }

    if (force_eof && !q.es_buf.empty())
    {
        q.es_buf.clear();
        q.anchors.clear();
        q.source_chunks.clear();
    }

    q.pending.active = !q.es_buf.empty();
}

void FlushPendingAuLocked(StreamQueue& q, bool force_eof = false)
{
    if (q.kind == StreamEsKind::kAvcAnnexB)
    {
        DrainAvcAusLocked(q, force_eof);
        return;
    }
    if (q.kind == StreamEsKind::kPspAtrac3Plus)
    {
        DrainAtracAusLocked(q, force_eof);
        return;
    }

    if (!q.pending.active)
    {
        return;
    }
    if (!q.pending.data.empty() && q.pending.has_pts)
    {
        AccessUnit au;
        au.pts = q.pending.pts;
        au.dts = q.pending.dts;
        au.data = std::move(q.pending.data);
        au.source_bytes = q.pending.source_bytes;
        q.ready.push_back(std::move(au));
    }
    q.pending = PendingAuBuffer{};
}

void PushPesPayloadFallbackLocked(StreamQueue& q,
                                  bool has_pts,
                                  uint64_t pts,
                                  uint64_t dts,
                                  const uint8_t* payload,
                                  size_t payload_size,
                                  uint32_t source_bytes)
{
    if (has_pts)
    {
        FlushPendingAuLocked(q, true);
        q.pending.active = true;
        q.pending.has_pts = true;
        q.pending.pts = pts;
        q.pending.dts = (dts != kInvalidTimestamp) ? dts : pts;
        q.pending.data.assign(payload, payload + payload_size);
        q.pending.source_bytes = source_bytes;
    }
    else if (q.pending.active)
    {
        q.pending.data.insert(q.pending.data.end(), payload, payload + payload_size);
        q.pending.source_bytes += source_bytes;
    }
}

void PushAvcPesPayloadLocked(StreamQueue& q,
                             bool has_pts,
                             uint64_t pts,
                             uint64_t dts,
                             const uint8_t* payload,
                             size_t payload_size,
                             uint32_t source_bytes)
{
    if (!q.es_framing_decided)
    {
        q.es_framing_decided = true;
        q.kind = (FindH264AudOffset(payload, payload_size, 0u) < payload_size)
                     ? StreamEsKind::kAvcAnnexB
                     : StreamEsKind::kFallbackPesPts;
    }

    if (q.kind == StreamEsKind::kFallbackPesPts)
    {
        PushPesPayloadFallbackLocked(q, has_pts, pts, dts, payload, payload_size, source_bytes);
        return;
    }

    const uint64_t chunk_start = q.es_read_pos + static_cast<uint64_t>(q.es_buf.size());
    q.es_buf.insert(q.es_buf.end(), payload, payload + payload_size);
    const uint64_t chunk_end = q.es_read_pos + static_cast<uint64_t>(q.es_buf.size());

    q.source_chunks.push_back(PesSourceChunk{ chunk_end, source_bytes });
    if (has_pts)
    {
        q.anchors.push_back(PesTimestampAnchor{
            chunk_start,
            chunk_end,
            pts,
            (dts != kInvalidTimestamp) ? dts : pts,
        });
    }

    DrainAvcAusLocked(q, false);
}

void PushAtracPesPayloadLocked(StreamQueue& q,
                               bool has_pts,
                               uint64_t pts,
                               uint64_t dts,
                               const uint8_t* payload,
                               size_t payload_size,
                               uint32_t first_au_offset,
                               uint32_t source_bytes)
{
    if (!q.es_framing_decided)
    {
        q.es_framing_decided = true;
        const bool has_magic_at_zero = (payload_size >= 8u && payload[0] == 0x0Fu && payload[1] == 0xD0u);
        const bool has_magic_at_off = (static_cast<size_t>(first_au_offset) + 8u <= payload_size &&
                                       payload[first_au_offset] == 0x0Fu &&
                                       payload[first_au_offset + 1u] == 0xD0u);
        q.kind = (has_magic_at_zero || has_magic_at_off)
                     ? StreamEsKind::kPspAtrac3Plus
                     : StreamEsKind::kFallbackPesPts;
    }

    if (q.kind == StreamEsKind::kFallbackPesPts)
    {
        PushPesPayloadFallbackLocked(q, has_pts, pts, dts, payload, payload_size, source_bytes);
        return;
    }

    const uint64_t chunk_start = q.es_read_pos + static_cast<uint64_t>(q.es_buf.size());
    q.es_buf.insert(q.es_buf.end(), payload, payload + payload_size);
    const uint64_t chunk_end = q.es_read_pos + static_cast<uint64_t>(q.es_buf.size());

    q.source_chunks.push_back(PesSourceChunk{ chunk_end, source_bytes });
    if (has_pts && first_au_offset < payload_size)
    {
        const uint64_t au_off = chunk_start + static_cast<uint64_t>(first_au_offset);
        q.anchors.push_back(PesTimestampAnchor{
            au_off,
            chunk_end,
            pts,
            (dts != kInvalidTimestamp) ? dts : pts,
        });
    }

    DrainAtracAusLocked(q, false);
}

void FlushAllPendingLocked(MpegInstance& inst);

void DemuxProgramStreamLocked(MpegInstance& inst,
                              RingbufferState* rb_state,
                              const uint8_t* bytes,
                              size_t size)
{
    inst.demux_scratch.insert(inst.demux_scratch.end(), bytes, bytes + size);
    const auto& buf = inst.demux_scratch;
    size_t pos = 0;

    while (pos + 4 <= buf.size())
    {
        if (!(buf[pos] == 0x00 && buf[pos + 1] == 0x00 && buf[pos + 2] == 0x01))
        {
            ++pos;
            continue;
        }

        const uint8_t start_code = buf[pos + 3];
        if (start_code == 0xBAu)
        {
            if (pos + 14 > buf.size())
            {
                break;
            }
            if ((buf[pos + 4] & 0xC0u) == 0x40u)
            {
                const size_t stuffing = buf[pos + 13] & 0x07u;
                const size_t header_len = 14u + stuffing;
                if (pos + header_len > buf.size())
                {
                    break;
                }
                pos += header_len;
            }
            else
            {
                pos += 12u;
            }
            continue;
        }

        if (start_code == 0xB9u)
        {
            pos += 4u;
            inst.hit_eof = true;
            FlushAllPendingLocked(inst);
            continue;
        }

        // Packets shorter than a PES length header or without a length field (< 0xBC).
        if (start_code < 0xBBu)
        {
            pos += 4u;
            continue;
        }

        if (pos + 6 > buf.size())
        {
            break;
        }

        const uint16_t packet_len = ReadBe16(&buf[pos + 4]);
        const size_t total_len = 6u + static_cast<size_t>(packet_len);
        if (pos + total_len > buf.size())
        {
            break;
        }

        if (start_code == 0xBBu || start_code == 0xBCu || start_code == 0xBEu || start_code == 0xBFu)
        {
            pos += total_len;
            continue;
        }

        const bool is_video = (start_code >= 0xE0u && start_code <= 0xEFu);
        const bool is_private1 = (start_code == 0xBDu);
        if ((is_video || is_private1) && total_len >= 9u && (buf[pos + 6] & 0xC0u) == 0x80u)
        {
            const uint8_t flags = buf[pos + 7];
            const uint8_t pts_dts_flags = (flags >> 6) & 0x03u;
            const size_t pes_header_data_len = buf[pos + 8];
            const size_t payload_offset = 9u + pes_header_data_len;
            if (payload_offset <= total_len)
            {
                bool has_pts = false;
                uint64_t pts = kInvalidTimestamp;
                uint64_t dts = kInvalidTimestamp;

                if ((pts_dts_flags == 0x02u || pts_dts_flags == 0x03u) &&
                    pes_header_data_len >= 5u && pos + 14u <= buf.size())
                {
                    pts = ParsePesTimestamp(&buf[pos + 9]);
                    has_pts = true;
                    if (pts_dts_flags == 0x03u && pes_header_data_len >= 10u && pos + 19u <= buf.size())
                    {
                        dts = ParsePesTimestamp(&buf[pos + 14]);
                    }
                    else
                    {
                        dts = pts;
                    }
                }

                const uint8_t* payload = &buf[pos + payload_offset];
                const size_t payload_size = total_len - payload_offset;

                if (is_video)
                {
                    const int32_t stream_num = static_cast<int32_t>(start_code & 0x0Fu);
                    PushAvcPesPayloadLocked(inst.avc_queues[stream_num],
                                            has_pts,
                                            pts,
                                            dts,
                                            payload,
                                            payload_size,
                                            static_cast<uint32_t>(total_len));
                    if (rb_state != nullptr)
                    {
                        rb_state->buffered_source_bytes += static_cast<uint32_t>(total_len);
                    }
                }
                else if (is_private1 && payload_size >= 4u)
                {
                    const uint8_t substream_id = payload[0];
                    const int32_t stream_num = static_cast<int32_t>(substream_id & 0x0Fu);
                    const uint32_t first_au_offset =
                        (static_cast<uint32_t>(payload[2]) << 8) | static_cast<uint32_t>(payload[3]);
                    const uint8_t* atrac_payload = payload + 4u;
                    const size_t atrac_size = payload_size - 4u;
                    PushAtracPesPayloadLocked(inst.atrac_queues[stream_num],
                                              has_pts,
                                              pts,
                                              dts,
                                              atrac_payload,
                                              atrac_size,
                                              first_au_offset,
                                              static_cast<uint32_t>(total_len));
                    if (rb_state != nullptr)
                    {
                        rb_state->buffered_source_bytes += static_cast<uint32_t>(total_len);
                    }
                }
            }
        }

        pos += total_len;
    }

    if (pos > 0)
    {
        inst.demux_scratch.erase(inst.demux_scratch.begin(),
                                 inst.demux_scratch.begin() + static_cast<ptrdiff_t>(pos));
    }
}

void FlushAllPendingLocked(MpegInstance& inst)
{
    for (auto& [stream_num, q] : inst.avc_queues)
    {
        (void)stream_num;
        FlushPendingAuLocked(q, true);
    }
    for (auto& [stream_num, q] : inst.atrac_queues)
    {
        (void)stream_num;
        FlushPendingAuLocked(q, true);
    }
}

void UpdateRingbufferAccountingLocked(runtime::GuestMemory& mem,
                                      MpegInstance* inst,
                                      RingbufferState* rb_state,
                                      GuestRingbuffer& guest_rb)
{
    bool any_aus_left = false;
    if (inst != nullptr)
    {
        for (const auto& [num, q] : inst->avc_queues)
        {
            (void)num;
            if (!q.ready.empty() || q.pending.active)
            {
                any_aus_left = true;
                break;
            }
        }
        if (!any_aus_left)
        {
            for (const auto& [num, q] : inst->atrac_queues)
            {
                (void)num;
                if (!q.ready.empty() || q.pending.active)
                {
                    any_aus_left = true;
                    break;
                }
            }
        }
    }

    if (rb_state != nullptr)
    {
        if (!any_aus_left)
        {
            rb_state->buffered_source_bytes = 0;
        }
        const int32_t unconsumed = std::clamp<int32_t>(
            static_cast<int32_t>(rb_state->buffered_source_bytes / kMpegPacketSize),
            0,
            std::max<int32_t>(0, rb_state->total_packets - 1));
        guest_rb.free_packets = std::max<int32_t>(0, rb_state->total_packets - unconsumed);
        if (!any_aus_left)
        {
            guest_rb.free_packets = rb_state->total_packets;
        }
        guest_rb.read_packets = rb_state->total_packets_read;
        guest_rb.write_packets = rb_state->total_packets_read;
        WriteGuestRingbuffer(mem, rb_state->rb_addr, guest_rb);
    }
}

RegisteredStream* FindStreamLocked(MpegInstance& inst, uint32_t stream_uid)
{
    for (auto& s : inst.streams)
    {
        if (s.active && s.uid == stream_uid)
        {
            return &s;
        }
    }
    return nullptr;
}

uint32_t BytesPerPixel(uint32_t pixel_format)
{
    return (pixel_format == repsp::codec::kAvcPixelFormat8888) ? 4u : 2u;
}

void HleMpegInit()
{
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.init_calls += 1;
    g_mpeg.initialized = true;
    recomp::Return(int32_t{ 0 });
}

void HleMpegFinish()
{
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.finish_calls += 1;
    g_mpeg.instances.clear();
    g_mpeg.ringbuffers.clear();
    g_mpeg.initialized = false;
    recomp::Return(int32_t{ 0 });
}

void HleMpegQueryMemSize()
{
    const int32_t mode = recomp::Arg<int32_t>(0);
    (void)mode;
    recomp::Return(kMpegMemSize);
}

void HleMpegCreate()
{
    const uint32_t mpeg_ptr_addr = recomp::Arg<uint32_t>(0);
    const uint32_t data_addr = recomp::Arg<uint32_t>(1);
    const int32_t size = recomp::Arg<int32_t>(2);
    const uint32_t rb_addr = recomp::Arg<uint32_t>(3);
    const int32_t frame_width = recomp::Arg<int32_t>(4);
    const int32_t mode = recomp::Arg<int32_t>(5);
    const int32_t ddr_top = recomp::Arg<int32_t>(6);
    (void)mode;
    (void)ddr_top;

    auto& mem = runtime::GetGuestMemory();
    if (mpeg_ptr_addr == 0 || !mem.IsValidAddress(mpeg_ptr_addr, 4) ||
        data_addr == 0 || size < kMpegMemSize || !mem.IsValidAddress(data_addr, static_cast<size_t>(size)))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);

    auto inst = std::make_shared<MpegInstance>();
    inst->mpeg_ptr_addr = mpeg_ptr_addr;
    inst->mpeg_data_addr = data_addr;
    inst->rb_addr = rb_addr;
    inst->frame_width = (frame_width > 0) ? static_cast<uint32_t>(frame_width) : kMpegDefaultFrameWidth;
    if (g_mpeg.last_psmf_info.valid)
    {
        inst->video_width = g_mpeg.last_psmf_info.width;
        inst->video_height = g_mpeg.last_psmf_info.height;
    }

    mem.WriteU32(mpeg_ptr_addr, data_addr);

    if (rb_addr != 0)
    {
        GuestRingbuffer guest_rb{};
        if (ReadGuestRingbuffer(mem, rb_addr, guest_rb))
        {
            guest_rb.mpeg_addr = data_addr;
            WriteGuestRingbuffer(mem, rb_addr, guest_rb);
        }
        if (auto rb = FindRingbufferLocked(rb_addr))
        {
            rb->linked_mpeg_addr = mpeg_ptr_addr;
        }
    }

    // Replace existing instance at the same pointer address if recreated.
    for (auto it = g_mpeg.instances.begin(); it != g_mpeg.instances.end(); ++it)
    {
        if ((*it)->mpeg_ptr_addr == mpeg_ptr_addr)
        {
            g_mpeg.instances.erase(it);
            break;
        }
    }
    g_mpeg.instances.push_back(inst);
    g_mpeg.create_calls += 1;
    recomp::Return(int32_t{ 0 });
}

void HleMpegDelete()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    for (auto it = g_mpeg.instances.begin(); it != g_mpeg.instances.end(); ++it)
    {
        if ((*it)->mpeg_ptr_addr == mpeg_addr || (*it)->mpeg_data_addr == mpeg_addr)
        {
            if ((*it)->rb_addr != 0)
            {
                if (auto rb = FindRingbufferLocked((*it)->rb_addr))
                {
                    rb->linked_mpeg_addr = 0;
                }
            }
            g_mpeg.instances.erase(it);
            break;
        }
    }
    g_mpeg.delete_calls += 1;
    recomp::Return(int32_t{ 0 });
}

void HleMpegRingbufferQueryMemSize()
{
    const int32_t packets = recomp::Arg<int32_t>(0);
    if (packets < 0)
    {
        recomp::Return(kSceMpegErrorInvalidValue);
        return;
    }
    const int32_t size = packets * (kMpegPacketSize + kMpegRingbufferPacketStructSize);
    recomp::Return(size);
}

void HleMpegRingbufferConstruct()
{
    const uint32_t rb_addr = recomp::Arg<uint32_t>(0);
    const int32_t packets = recomp::Arg<int32_t>(1);
    const uint32_t data_addr = recomp::Arg<uint32_t>(2);
    const int32_t size = recomp::Arg<int32_t>(3);
    const uint32_t callback_addr = recomp::Arg<uint32_t>(4);
    const uint32_t callback_data = recomp::Arg<uint32_t>(5);
    (void)size;

    auto& mem = runtime::GetGuestMemory();
    if (rb_addr == 0 || !mem.IsValidAddress(rb_addr, sizeof(GuestRingbuffer)))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }
    if (packets <= 0)
    {
        recomp::Return(kSceMpegErrorInvalidValue);
        return;
    }
    const size_t data_bytes = static_cast<size_t>(packets) * static_cast<size_t>(kMpegPacketSize);
    if (data_addr == 0 || !mem.IsValidAddress(data_addr, data_bytes))
    {
        recomp::Return(kSceMpegErrorNoMemory);
        return;
    }

    GuestRingbuffer guest_rb{};
    guest_rb.packets = packets;
    guest_rb.read_packets = 0;
    guest_rb.write_packets = 0;
    guest_rb.free_packets = packets;
    guest_rb.packet_size = kMpegPacketSize;
    guest_rb.data_addr = data_addr;
    guest_rb.callback_addr = callback_addr;
    guest_rb.callback_data = callback_data;
    guest_rb.data_upper_bound = data_addr + static_cast<uint32_t>(data_bytes);
    guest_rb.sema_id = -1;
    guest_rb.mpeg_addr = 0;
    WriteGuestRingbuffer(mem, rb_addr, guest_rb);

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    for (auto it = g_mpeg.ringbuffers.begin(); it != g_mpeg.ringbuffers.end(); ++it)
    {
        if ((*it)->rb_addr == rb_addr)
        {
            g_mpeg.ringbuffers.erase(it);
            break;
        }
    }
    auto rb = std::make_shared<RingbufferState>();
    rb->rb_addr = rb_addr;
    rb->total_packets = packets;
    rb->data_addr = data_addr;
    rb->callback_addr = callback_addr;
    rb->callback_data = callback_data;
    g_mpeg.ringbuffers.push_back(rb);

    recomp::Return(int32_t{ 0 });
}

void HleMpegRingbufferDestruct()
{
    const uint32_t rb_addr = recomp::Arg<uint32_t>(0);
    auto& mem = runtime::GetGuestMemory();

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    for (auto it = g_mpeg.ringbuffers.begin(); it != g_mpeg.ringbuffers.end(); ++it)
    {
        if ((*it)->rb_addr == rb_addr)
        {
            g_mpeg.ringbuffers.erase(it);
            break;
        }
    }

    GuestRingbuffer guest_rb{};
    if (ReadGuestRingbuffer(mem, rb_addr, guest_rb))
    {
        guest_rb.read_packets = 0;
        guest_rb.write_packets = 0;
        guest_rb.free_packets = guest_rb.packets;
        guest_rb.mpeg_addr = 0;
        WriteGuestRingbuffer(mem, rb_addr, guest_rb);
    }
    recomp::Return(int32_t{ 0 });
}

void HleMpegRingbufferAvailableSize()
{
    const uint32_t rb_addr = recomp::Arg<uint32_t>(0);
    auto& mem = runtime::GetGuestMemory();

    GuestRingbuffer guest_rb{};
    if (!ReadGuestRingbuffer(mem, rb_addr, guest_rb))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    auto rb = FindRingbufferLocked(rb_addr);
    auto inst = FindInstanceByRingbufferLocked(rb_addr);
    UpdateRingbufferAccountingLocked(mem, inst.get(), rb.get(), guest_rb);
    recomp::Return(guest_rb.free_packets);
}

void HleMpegRingbufferPut()
{
    const uint32_t rb_addr = recomp::Arg<uint32_t>(0);
    const int32_t num_packets = recomp::Arg<int32_t>(1);
    const int32_t available = recomp::Arg<int32_t>(2);

    auto& mem = runtime::GetGuestMemory();
    GuestRingbuffer guest_rb{};
    if (!ReadGuestRingbuffer(mem, rb_addr, guest_rb))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    int32_t to_read = 0;
    uint32_t callback_addr = 0;
    uint32_t data_addr = 0;
    uint32_t callback_data = 0;

    {
        std::lock_guard<std::mutex> lock(g_mpeg.mutex);
        g_mpeg.ringbuffer_put_calls += 1;

        if (num_packets <= 0 || available <= 0 || guest_rb.packets <= 0)
        {
            recomp::Return(int32_t{ 0 });
            return;
        }

        to_read = std::min({ num_packets, available, guest_rb.packets });
        callback_addr = guest_rb.callback_addr;
        data_addr = guest_rb.data_addr;
        callback_data = guest_rb.callback_data;
    }

    if (callback_addr == 0)
    {
        recomp::Return(int32_t{ 0 });
        return;
    }

    // Invoke guest ringbuffer callback in the calling guest thread context with mutex unlocked
    // so inner blocking calls (sceIoRead / WaitEventFlag / DelayThread) execute normally.
    const int32_t packets_added = runtime::GetScheduler().CallGuestFunction(
        callback_addr,
        data_addr,
        static_cast<uint32_t>(to_read),
        callback_data,
        0);

    if (packets_added < 0)
    {
        recomp::Return(packets_added);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    auto rb = FindRingbufferLocked(rb_addr);
    auto inst = FindInstanceByRingbufferLocked(rb_addr);

    if (packets_added == 0)
    {
        if (inst != nullptr)
        {
            inst->hit_eof = true;
            FlushAllPendingLocked(*inst);
        }
        recomp::Return(int32_t{ 0 });
        return;
    }

    const int32_t clamped = std::min(packets_added, guest_rb.packets);
    const size_t bytes_added = static_cast<size_t>(clamped) * static_cast<size_t>(kMpegPacketSize);

    if (inst != nullptr && data_addr != 0 && mem.IsValidAddress(data_addr, bytes_added))
    {
        const uint8_t* ptr = mem.Translate(data_addr);
        if (ptr != nullptr)
        {
            DemuxProgramStreamLocked(*inst, rb.get(), ptr, bytes_added);
        }
    }

    if (rb != nullptr)
    {
        rb->total_packets_read += clamped;
    }
    g_mpeg.ringbuffer_packets_added += static_cast<uint64_t>(clamped);
    UpdateRingbufferAccountingLocked(mem, inst.get(), rb.get(), guest_rb);

    recomp::Return(clamped);
}

void HleMpegQueryStreamOffset()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t buf_addr = recomp::Arg<uint32_t>(1);
    const uint32_t offset_out_addr = recomp::Arg<uint32_t>(2);

    auto& mem = runtime::GetGuestMemory();
    if (buf_addr == 0 || !mem.IsValidAddress(buf_addr, 0x80u) ||
        offset_out_addr == 0 || !mem.IsValidAddress(offset_out_addr, 4u))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    const size_t inspect_size = mem.IsValidAddress(buf_addr, kMpegPacketSize)
                                    ? static_cast<size_t>(kMpegPacketSize)
                                    : static_cast<size_t>(0x80u);
    const uint8_t* ptr = mem.Translate(buf_addr);
    PsmfHeaderInfo info{};
    if (ptr == nullptr || !ParsePsmfHeader(std::span<const uint8_t>(ptr, inspect_size), info))
    {
        mem.WriteU32(offset_out_addr, 0);
        recomp::Return(kSceMpegErrorBadVersion);
        return;
    }

    mem.WriteU32(offset_out_addr, info.stream_offset);

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.last_psmf_info = info;
    g_mpeg.psmf_headers_parsed += 1;
    if (auto inst = FindInstanceLocked(mpeg_addr))
    {
        inst->video_width = info.width;
        inst->video_height = info.height;
    }

    recomp::Return(int32_t{ 0 });
}

void HleMpegQueryStreamSize()
{
    const uint32_t buf_addr = recomp::Arg<uint32_t>(0);
    const uint32_t size_out_addr = recomp::Arg<uint32_t>(1);

    auto& mem = runtime::GetGuestMemory();
    if (buf_addr == 0 || !mem.IsValidAddress(buf_addr, 0x80u) ||
        size_out_addr == 0 || !mem.IsValidAddress(size_out_addr, 4u))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    const size_t inspect_size = mem.IsValidAddress(buf_addr, kMpegPacketSize)
                                    ? static_cast<size_t>(kMpegPacketSize)
                                    : static_cast<size_t>(0x80u);
    const uint8_t* ptr = mem.Translate(buf_addr);
    PsmfHeaderInfo info{};
    if (ptr == nullptr || !ParsePsmfHeader(std::span<const uint8_t>(ptr, inspect_size), info))
    {
        mem.WriteU32(size_out_addr, 0);
        recomp::Return(kSceMpegErrorBadVersion);
        return;
    }

    mem.WriteU32(size_out_addr, info.stream_size);

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.last_psmf_info = info;

    recomp::Return(int32_t{ 0 });
}

void HleMpegRegistStream()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const int32_t stream_type = recomp::Arg<int32_t>(1);
    const int32_t stream_num = recomp::Arg<int32_t>(2);

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    auto inst = FindInstanceLocked(mpeg_addr);
    if (!inst)
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    RegisteredStream stream{};
    stream.uid = inst->next_stream_uid++;
    stream.stream_type = stream_type;
    stream.stream_num = stream_num;
    stream.active = true;
    inst->streams.push_back(stream);
    g_mpeg.streams_registered += 1;

    recomp::Return(stream.uid);
}

void HleMpegUnRegistStream()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t stream_uid = recomp::Arg<uint32_t>(1);

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    auto inst = FindInstanceLocked(mpeg_addr);
    if (!inst)
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    for (auto& s : inst->streams)
    {
        if (s.uid == stream_uid)
        {
            s.active = false;
            break;
        }
    }
    recomp::Return(int32_t{ 0 });
}

void HleMpegMallocAvcEsBuf()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    auto inst = FindInstanceLocked(mpeg_addr);
    if (!inst)
    {
        recomp::Return(uint32_t{ 0 });
        return;
    }

    const uint32_t handle = inst->next_es_handle++;
    inst->es_handles.push_back(handle);
    recomp::Return(handle);
}

void HleMpegFreeAvcEsBuf()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t es_buf = recomp::Arg<uint32_t>(1);

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    if (auto inst = FindInstanceLocked(mpeg_addr))
    {
        auto it = std::find(inst->es_handles.begin(), inst->es_handles.end(), es_buf);
        if (it != inst->es_handles.end())
        {
            inst->es_handles.erase(it);
        }
    }
    recomp::Return(int32_t{ 0 });
}

void HleMpegQueryAtracEsSize()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t es_size_addr = recomp::Arg<uint32_t>(1);
    const uint32_t out_size_addr = recomp::Arg<uint32_t>(2);
    (void)mpeg_addr;

    auto& mem = runtime::GetGuestMemory();
    if (es_size_addr == 0 || !mem.IsValidAddress(es_size_addr, 4u) ||
        out_size_addr == 0 || !mem.IsValidAddress(out_size_addr, 4u))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    mem.WriteU32(es_size_addr, static_cast<uint32_t>(kMpegAtracEsSize));
    mem.WriteU32(out_size_addr, static_cast<uint32_t>(kMpegAtracOutSize));
    recomp::Return(int32_t{ 0 });
}

void HleMpegInitAu()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t es_buffer = recomp::Arg<uint32_t>(1);
    const uint32_t au_addr = recomp::Arg<uint32_t>(2);
    (void)mpeg_addr;

    auto& mem = runtime::GetGuestMemory();
    if (au_addr == 0 || !mem.IsValidAddress(au_addr, sizeof(GuestAu)))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    GuestAu au{};
    au.pts_msb = 0xFFFFFFFFu;
    au.pts_lsb = 0xFFFFFFFFu;
    au.dts_msb = 0xFFFFFFFFu;
    au.dts_lsb = 0xFFFFFFFFu;
    au.es_buffer = es_buffer;
    au.es_size = 0;
    WriteGuestAu(mem, au_addr, au);
    recomp::Return(int32_t{ 0 });
}

void HleMpegGetAvcAu()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t stream_uid = recomp::Arg<uint32_t>(1);
    const uint32_t au_addr = recomp::Arg<uint32_t>(2);
    const uint32_t attr_addr = recomp::Arg<uint32_t>(3);

    auto& mem = runtime::GetGuestMemory();
    GuestAu guest_au{};
    if (!ReadGuestAu(mem, au_addr, guest_au))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.avc_au_calls += 1;

    auto inst = FindInstanceLocked(mpeg_addr);
    if (!inst)
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    const RegisteredStream* stream = FindStreamLocked(*inst, stream_uid);
    const int32_t stream_num = (stream != nullptr) ? stream->stream_num : 0;

    auto& q = inst->avc_queues[stream_num];
    if (q.ready.empty() && (q.kind == StreamEsKind::kFallbackPesPts || inst->hit_eof))
    {
        FlushPendingAuLocked(q, inst->hit_eof);
    }

    if (q.ready.empty())
    {
        recomp::Return(kSceMpegErrorNoData);
        return;
    }

    AccessUnit au = std::move(q.ready.front());
    q.ready.pop_front();

    if (auto rb = FindRingbufferLocked(inst->rb_addr))
    {
        rb->buffered_source_bytes = (rb->buffered_source_bytes > au.source_bytes)
                                        ? (rb->buffered_source_bytes - au.source_bytes)
                                        : 0u;
    }

    guest_au.pts_msb = static_cast<uint32_t>((au.pts >> 32) & 0xFFFFFFFFull);
    guest_au.pts_lsb = static_cast<uint32_t>(au.pts & 0xFFFFFFFFull);
    guest_au.dts_msb = static_cast<uint32_t>((au.dts >> 32) & 0xFFFFFFFFull);
    guest_au.dts_lsb = static_cast<uint32_t>(au.dts & 0xFFFFFFFFull);
    guest_au.es_size = static_cast<uint32_t>(au.data.size());
    WriteGuestAu(mem, au_addr, guest_au);

    if (attr_addr != 0 && mem.IsValidAddress(attr_addr, 4u))
    {
        mem.WriteU32(attr_addr, 1u);
    }

    inst->last_avc_aus[au_addr] = au;
    inst->last_avc_au = std::move(au);
    g_mpeg.avc_aus_returned += 1;

    recomp::Return(int32_t{ 0 });
}

void HleMpegGetAtracAu()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t stream_uid = recomp::Arg<uint32_t>(1);
    const uint32_t au_addr = recomp::Arg<uint32_t>(2);
    const uint32_t attr_addr = recomp::Arg<uint32_t>(3);

    auto& mem = runtime::GetGuestMemory();
    GuestAu guest_au{};
    if (!ReadGuestAu(mem, au_addr, guest_au))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.atrac_au_calls += 1;

    auto inst = FindInstanceLocked(mpeg_addr);
    if (!inst)
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    const RegisteredStream* stream = FindStreamLocked(*inst, stream_uid);
    const int32_t stream_num = (stream != nullptr) ? stream->stream_num : 0;

    auto& q = inst->atrac_queues[stream_num];
    if (q.ready.empty() && (q.kind == StreamEsKind::kFallbackPesPts || inst->hit_eof))
    {
        FlushPendingAuLocked(q, inst->hit_eof);
    }

    if (q.ready.empty())
    {
        recomp::Return(kSceMpegErrorNoData);
        return;
    }

    AccessUnit au = std::move(q.ready.front());
    q.ready.pop_front();

    if (auto rb = FindRingbufferLocked(inst->rb_addr))
    {
        rb->buffered_source_bytes = (rb->buffered_source_bytes > au.source_bytes)
                                        ? (rb->buffered_source_bytes - au.source_bytes)
                                        : 0u;
    }

    guest_au.pts_msb = static_cast<uint32_t>((au.pts >> 32) & 0xFFFFFFFFull);
    guest_au.pts_lsb = static_cast<uint32_t>(au.pts & 0xFFFFFFFFull);
    guest_au.dts_msb = static_cast<uint32_t>((au.dts >> 32) & 0xFFFFFFFFull);
    guest_au.dts_lsb = static_cast<uint32_t>(au.dts & 0xFFFFFFFFull);
    guest_au.es_size = static_cast<uint32_t>(au.data.size());
    WriteGuestAu(mem, au_addr, guest_au);

    if (guest_au.es_buffer >= runtime::GuestMemory::kKernelRamAddress && !au.data.empty())
    {
        const size_t copy_bytes = std::min<size_t>(au.data.size(), static_cast<size_t>(kMpegAtracEsSize));
        if (mem.IsValidAddress(guest_au.es_buffer, copy_bytes))
        {
            mem.WriteBlock(guest_au.es_buffer, au.data.data(), copy_bytes);
        }
    }

    if (attr_addr != 0 && mem.IsValidAddress(attr_addr, 4u))
    {
        mem.WriteU32(attr_addr, 0u);
    }

    inst->last_atrac_au = std::move(au);
    g_mpeg.atrac_aus_returned += 1;

    recomp::Return(int32_t{ 0 });
}

void HleMpegAvcDecode()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t au_addr = recomp::Arg<uint32_t>(1);
    const int32_t frame_width_arg = recomp::Arg<int32_t>(2);
    const uint32_t buf_arg_addr = recomp::Arg<uint32_t>(3);
    const uint32_t init_addr = recomp::Arg<uint32_t>(4);

    auto& mem = runtime::GetGuestMemory();

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.avc_decode_calls += 1;

    auto inst = FindInstanceLocked(mpeg_addr);
    const uint32_t width = (frame_width_arg > 0)
                               ? static_cast<uint32_t>(frame_width_arg)
                               : (inst ? inst->frame_width : kMpegDefaultFrameWidth);
    const uint32_t height = (inst && inst->video_height > 0)
                                ? inst->video_height
                                : kMpegDefaultHeight;
    const uint32_t pixel_format = inst ? inst->pixel_format : repsp::codec::kAvcPixelFormat8888;
    const size_t out_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * BytesPerPixel(pixel_format);

    // On PSP, sceMpegAvcDecode receives void** buf (a guest pointer holding the frame buffer address).
    // Accept a direct buffer address as a fallback if indirect dereference does not yield a valid buffer.
    uint32_t dst_addr = 0;
    if (buf_arg_addr != 0 && mem.IsValidAddress(buf_arg_addr, 4u))
    {
        const uint32_t indirect = mem.ReadU32(buf_arg_addr);
        if (indirect != 0 && mem.IsValidAddress(indirect, out_bytes))
        {
            dst_addr = indirect;
        }
        else if (mem.IsValidAddress(buf_arg_addr, out_bytes))
        {
            dst_addr = buf_arg_addr;
        }
    }

    if (dst_addr == 0)
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    std::span<const uint8_t> au_span;
    if (inst != nullptr)
    {
        auto it = inst->last_avc_aus.find(au_addr);
        if (it != inst->last_avc_aus.end())
        {
            au_span = std::span<const uint8_t>(it->second.data);
        }
        else
        {
            au_span = std::span<const uint8_t>(inst->last_avc_au.data);
        }
    }

    uint8_t* dst_ptr = mem.Translate(dst_addr);
    repsp::codec::AvcDecoder* dec = (g_mpeg.avc_decoder != nullptr)
                                         ? g_mpeg.avc_decoder
                                         : &g_mpeg.default_avc_decoder;
    if (dst_ptr == nullptr || !dec->DecodeAu(au_span, width, height, pixel_format, std::span<uint8_t>(dst_ptr, out_bytes)))
    {
        recomp::Return(kSceMpegErrorInvalidValue);
        return;
    }

    if (init_addr != 0 && mem.IsValidAddress(init_addr, 4u))
    {
        mem.WriteU32(init_addr, 1u);
    }

    g_mpeg.avc_frames_decoded += 1;
    recomp::Return(int32_t{ 0 });
}

void HleMpegAvcDecodeStop()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const int32_t frame_width = recomp::Arg<int32_t>(1);
    const uint32_t buf_addr = recomp::Arg<uint32_t>(2);
    const uint32_t status_addr = recomp::Arg<uint32_t>(3);
    (void)mpeg_addr;
    (void)frame_width;
    (void)buf_addr;

    auto& mem = runtime::GetGuestMemory();
    if (status_addr != 0 && mem.IsValidAddress(status_addr, 4u))
    {
        mem.WriteU32(status_addr, 0u);
    }
    recomp::Return(int32_t{ 0 });
}

void HleMpegAvcDecodeFlush()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    if (auto inst = FindInstanceLocked(mpeg_addr))
    {
        inst->avc_queues.clear();
        inst->last_avc_aus.clear();
    }
    repsp::codec::AvcDecoder* dec = (g_mpeg.avc_decoder != nullptr)
                                         ? g_mpeg.avc_decoder
                                         : &g_mpeg.default_avc_decoder;
    dec->Flush();
    recomp::Return(int32_t{ 0 });
}

void HleMpegAtracDecode()
{
    const uint32_t mpeg_addr = recomp::Arg<uint32_t>(0);
    const uint32_t au_addr = recomp::Arg<uint32_t>(1);
    const uint32_t buf_addr = recomp::Arg<uint32_t>(2);
    const int32_t init = recomp::Arg<int32_t>(3);
    (void)init;

    auto& mem = runtime::GetGuestMemory();
    if (buf_addr == 0 || !mem.IsValidAddress(buf_addr, static_cast<size_t>(kMpegAtracOutSize)))
    {
        recomp::Return(kSceMpegErrorInvalidAddr);
        return;
    }

    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.atrac_decode_calls += 1;

    GuestAu guest_au{};
    ReadGuestAu(mem, au_addr, guest_au);

    auto inst = FindInstanceLocked(mpeg_addr);
    const uint8_t* frame_ptr = nullptr;
    size_t frame_size = 0;
    if (inst != nullptr && !inst->last_atrac_au.data.empty())
    {
        frame_ptr = inst->last_atrac_au.data.data();
        frame_size = inst->last_atrac_au.data.size();
    }

    repsp::codec::AtracFrameInfo frame_info{};
    frame_info.channels = 2;
    frame_info.sample_rate = 44100;
    frame_info.samples_per_frame = 2048;

    auto* out_pcm = reinterpret_cast<int16_t*>(mem.Translate(buf_addr));
    repsp::codec::AtracDecoder* dec = (g_mpeg.atrac_decoder != nullptr)
                                           ? g_mpeg.atrac_decoder
                                           : &g_mpeg.default_atrac_decoder;
    if (out_pcm == nullptr || !dec->DecodeFrame(frame_info, frame_ptr, frame_size, out_pcm, 2048))
    {
        recomp::Return(kSceMpegErrorInvalidValue);
        return;
    }

    recomp::Return(int32_t{ 0 });
}

} // namespace

bool ParsePsmfHeader(std::span<const uint8_t> data, PsmfHeaderInfo& out)
{
    out = PsmfHeaderInfo{};
    if (data.size() < 0x80u)
    {
        return false;
    }
    if (data[0] != 'P' || data[1] != 'S' || data[2] != 'M' || data[3] != 'F')
    {
        return false;
    }
    if (!IsValidPsmfVersion(&data[4]))
    {
        return false;
    }

    out.valid = true;
    std::memcpy(out.version, &data[4], 4u);
    out.version[4] = '\0';
    out.stream_offset = ReadBe32(&data[0x08]);
    out.stream_size = ReadBe32(&data[0x0C]);

    if (data.size() >= 0x60u)
    {
        out.first_pts = ReadBe48(&data[0x54]);
        out.last_pts = ReadBe48(&data[0x5A]);
    }

    if (data.size() >= 0x82u)
    {
        out.stream_count = ReadBe16(&data[0x80]);
    }

    if (data.size() >= 0x92u)
    {
        const uint32_t w = static_cast<uint32_t>(data[0x8E]) * 16u;
        const uint32_t h = static_cast<uint32_t>(data[0x8F]) * 16u;
        if (w >= 16u && w <= 512u && h >= 16u && h <= 512u)
        {
            out.width = w;
            out.height = h;
        }
    }

    return true;
}

void Register()
{
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.instances.clear();
    g_mpeg.ringbuffers.clear();
    g_mpeg.last_psmf_info = PsmfHeaderInfo{};
    g_mpeg.initialized = false;
    g_mpeg.init_calls = 0;
    g_mpeg.finish_calls = 0;
    g_mpeg.create_calls = 0;
    g_mpeg.delete_calls = 0;
    g_mpeg.ringbuffer_put_calls = 0;
    g_mpeg.ringbuffer_packets_added = 0;
    g_mpeg.psmf_headers_parsed = 0;
    g_mpeg.streams_registered = 0;
    g_mpeg.avc_au_calls = 0;
    g_mpeg.avc_aus_returned = 0;
    g_mpeg.atrac_au_calls = 0;
    g_mpeg.atrac_aus_returned = 0;
    g_mpeg.avc_decode_calls = 0;
    g_mpeg.avc_frames_decoded = 0;
    g_mpeg.atrac_decode_calls = 0;
    g_mpeg.registered = true;

    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceMpeg", "sceMpegInit", HleMpegInit);
    reg.RegisterByName("sceMpeg", "sceMpegFinish", HleMpegFinish);
    reg.RegisterByName("sceMpeg", "sceMpegQueryMemSize", HleMpegQueryMemSize);
    reg.RegisterByName("sceMpeg", "sceMpegCreate", HleMpegCreate);
    reg.RegisterByName("sceMpeg", "sceMpegDelete", HleMpegDelete);
    reg.RegisterByName("sceMpeg", "sceMpegRingbufferQueryMemSize", HleMpegRingbufferQueryMemSize);
    reg.RegisterByName("sceMpeg", "sceMpegRingbufferConstruct", HleMpegRingbufferConstruct);
    reg.RegisterByName("sceMpeg", "sceMpegRingbufferDestruct", HleMpegRingbufferDestruct);
    reg.RegisterByName("sceMpeg", "sceMpegRingbufferAvailableSize", HleMpegRingbufferAvailableSize);
    reg.RegisterByName("sceMpeg", "sceMpegRingbufferPut", HleMpegRingbufferPut);
    reg.RegisterByName("sceMpeg", "sceMpegQueryStreamOffset", HleMpegQueryStreamOffset);
    reg.RegisterByName("sceMpeg", "sceMpegQueryStreamSize", HleMpegQueryStreamSize);
    reg.RegisterByName("sceMpeg", "sceMpegRegistStream", HleMpegRegistStream);
    reg.RegisterByName("sceMpeg", "sceMpegUnRegistStream", HleMpegUnRegistStream);
    reg.RegisterByName("sceMpeg", "sceMpegMallocAvcEsBuf", HleMpegMallocAvcEsBuf);
    reg.RegisterByName("sceMpeg", "sceMpegFreeAvcEsBuf", HleMpegFreeAvcEsBuf);
    reg.RegisterByName("sceMpeg", "sceMpegQueryAtracEsSize", HleMpegQueryAtracEsSize);
    reg.RegisterByName("sceMpeg", "sceMpegInitAu", HleMpegInitAu);
    reg.RegisterByName("sceMpeg", "sceMpegGetAvcAu", HleMpegGetAvcAu);
    reg.RegisterByName("sceMpeg", "sceMpegGetAtracAu", HleMpegGetAtracAu);
    reg.RegisterByName("sceMpeg", "sceMpegAvcDecode", HleMpegAvcDecode);
    reg.RegisterByName("sceMpeg", "sceMpegAvcDecodeStop", HleMpegAvcDecodeStop);
    reg.RegisterByName("sceMpeg", "sceMpegAvcDecodeFlush", HleMpegAvcDecodeFlush);
    reg.RegisterByName("sceMpeg", "sceMpegAtracDecode", HleMpegAtracDecode);
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.instances.clear();
    g_mpeg.ringbuffers.clear();
    g_mpeg.avc_decoder = &g_mpeg.default_avc_decoder;
    g_mpeg.atrac_decoder = &g_mpeg.default_atrac_decoder;
    g_mpeg.initialized = false;
    g_mpeg.registered = false;
}

void SetAvcDecoder(repsp::codec::AvcDecoder* decoder)
{
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.avc_decoder = (decoder != nullptr) ? decoder : &g_mpeg.default_avc_decoder;
}

void SetAtracDecoder(repsp::codec::AtracDecoder* decoder)
{
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    g_mpeg.atrac_decoder = (decoder != nullptr) ? decoder : &g_mpeg.default_atrac_decoder;
}

MpegStats GetStats()
{
    std::lock_guard<std::mutex> lock(g_mpeg.mutex);
    MpegStats s{};
    s.init_calls = g_mpeg.init_calls;
    s.finish_calls = g_mpeg.finish_calls;
    s.create_calls = g_mpeg.create_calls;
    s.delete_calls = g_mpeg.delete_calls;
    s.ringbuffer_put_calls = g_mpeg.ringbuffer_put_calls;
    s.ringbuffer_packets_added = g_mpeg.ringbuffer_packets_added;
    s.psmf_headers_parsed = g_mpeg.psmf_headers_parsed;
    s.streams_registered = g_mpeg.streams_registered;
    s.avc_au_calls = g_mpeg.avc_au_calls;
    s.avc_aus_returned = g_mpeg.avc_aus_returned;
    s.atrac_au_calls = g_mpeg.atrac_au_calls;
    s.atrac_aus_returned = g_mpeg.atrac_aus_returned;
    s.avc_decode_calls = g_mpeg.avc_decode_calls;
    s.avc_frames_decoded = g_mpeg.avc_frames_decoded;
    s.atrac_decode_calls = g_mpeg.atrac_decode_calls;
    s.psmf_stream_offset = g_mpeg.last_psmf_info.stream_offset;
    s.psmf_stream_size = g_mpeg.last_psmf_info.stream_size;
    s.psmf_width = g_mpeg.last_psmf_info.width;
    s.psmf_height = g_mpeg.last_psmf_info.height;
    const auto* avc_dec = (g_mpeg.avc_decoder != nullptr) ? g_mpeg.avc_decoder : &g_mpeg.default_avc_decoder;
    const auto* atrac_dec = (g_mpeg.atrac_decoder != nullptr) ? g_mpeg.atrac_decoder : &g_mpeg.default_atrac_decoder;
    s.avc_decoder_name = avc_dec->Name();
    s.atrac_decoder_name = atrac_dec->Name();
    return s;
}

std::string FormatStats()
{
    const MpegStats s = GetStats();
    char buf[384];
    std::snprintf(buf,
                  sizeof(buf),
                  "AVC decode: %s | ATRAC decode: %s | init=%llu create=%llu "
                  "rbPut=%llu packets=%llu psmf=%ux%u(off=0x%X,size=0x%X) "
                  "avcAu=%llu/%llu atracAu=%llu/%llu avcDec=%llu atracDec=%llu",
                  s.avc_decoder_name.c_str(),
                  s.atrac_decoder_name.c_str(),
                  static_cast<unsigned long long>(s.init_calls),
                  static_cast<unsigned long long>(s.create_calls),
                  static_cast<unsigned long long>(s.ringbuffer_put_calls),
                  static_cast<unsigned long long>(s.ringbuffer_packets_added),
                  s.psmf_width,
                  s.psmf_height,
                  s.psmf_stream_offset,
                  s.psmf_stream_size,
                  static_cast<unsigned long long>(s.avc_aus_returned),
                  static_cast<unsigned long long>(s.avc_au_calls),
                  static_cast<unsigned long long>(s.atrac_aus_returned),
                  static_cast<unsigned long long>(s.atrac_au_calls),
                  static_cast<unsigned long long>(s.avc_frames_decoded),
                  static_cast<unsigned long long>(s.atrac_decode_calls));
    return std::string(buf);
}

} // namespace repsp::hle::mpeg
