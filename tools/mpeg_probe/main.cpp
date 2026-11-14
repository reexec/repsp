#include "repsp/codec/atrac_decoder.h"
#include "repsp/codec/avc_decoder.h"
#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/hle/mpeg.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/sha1.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define TEST_ASSERT(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::exit(1); \
        } \
    } while (0)

namespace
{

struct MockCpuState
{
    uint32_t gpr[32] = {};
    uint32_t fpr[32] = {};
};

MockCpuState g_cpu;

void MockSave(void* ctx)
{
    std::memcpy(ctx, &g_cpu, sizeof(g_cpu));
}

void MockRestore(const void* ctx)
{
    std::memcpy(&g_cpu, ctx, sizeof(g_cpu));
}

uint32_t MockGetGpr(int idx)
{
    return (idx >= 0 && idx < 32) ? g_cpu.gpr[idx] : 0u;
}

void MockSetGpr(int idx, uint32_t v)
{
    if (idx > 0 && idx < 32)
    {
        g_cpu.gpr[idx] = v;
    }
}

uint32_t MockGetFpr(int idx)
{
    return (idx >= 0 && idx < 32) ? g_cpu.fpr[idx] : 0u;
}

void MockSetFpr(int idx, uint32_t v)
{
    if (idx >= 0 && idx < 32)
    {
        g_cpu.fpr[idx] = v;
    }
}

uint32_t HleCall(const char* name,
                 uint32_t a0 = 0,
                 uint32_t a1 = 0,
                 uint32_t a2 = 0,
                 uint32_t a3 = 0,
                 uint32_t t0 = 0,
                 uint32_t t1 = 0)
{
    g_cpu.gpr[4] = a0;
    g_cpu.gpr[5] = a1;
    g_cpu.gpr[6] = a2;
    g_cpu.gpr[7] = a3;
    g_cpu.gpr[8] = t0;
    g_cpu.gpr[9] = t1;
    repsp_recomp_hle_call(repsp::runtime::ComputeNid(name));
    return g_cpu.gpr[2];
}

void WriteBe16(uint8_t* dst, uint16_t v)
{
    dst[0] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[1] = static_cast<uint8_t>(v & 0xFFu);
}

void WriteBe32(uint8_t* dst, uint32_t v)
{
    dst[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    dst[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[3] = static_cast<uint8_t>(v & 0xFFu);
}

void WriteBe48(uint8_t* dst, uint64_t v)
{
    dst[0] = static_cast<uint8_t>((v >> 40) & 0xFFu);
    dst[1] = static_cast<uint8_t>((v >> 32) & 0xFFu);
    dst[2] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    dst[3] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[4] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[5] = static_cast<uint8_t>(v & 0xFFu);
}

void EncodePesPts(uint8_t* dst, uint64_t pts)
{
    dst[0] = static_cast<uint8_t>(0x21u | (((pts >> 30) & 0x07u) << 1));
    dst[1] = static_cast<uint8_t>((pts >> 22) & 0xFFu);
    dst[2] = static_cast<uint8_t>(0x01u | (((pts >> 15) & 0x7Fu) << 1));
    dst[3] = static_cast<uint8_t>((pts >> 7) & 0xFFu);
    dst[4] = static_cast<uint8_t>(0x01u | ((pts & 0x7Fu) << 1));
}

void BuildSyntheticPsmfHeader(std::array<uint8_t, 2048>& hdr)
{
    hdr.fill(0);
    hdr[0] = 'P';
    hdr[1] = 'S';
    hdr[2] = 'M';
    hdr[3] = 'F';
    hdr[4] = '0';
    hdr[5] = '0';
    hdr[6] = '1';
    hdr[7] = '4';
    WriteBe32(&hdr[0x08], 0x800u);
    WriteBe32(&hdr[0x0C], 2048u);
    WriteBe48(&hdr[0x54], 90000ull);
    WriteBe48(&hdr[0x5A], 900000ull);
    WriteBe16(&hdr[0x80], 2u);
    hdr[0x8E] = 30u; // 30 * 16 = 480 px
    hdr[0x8F] = 17u; // 17 * 16 = 272 px
}

void BuildSyntheticPsPack(std::array<uint8_t, 2048>& pack, uint64_t pts, uint8_t avc_fill, uint8_t atrac_fill)
{
    pack.fill(0);
    size_t pos = 0;

    // MPEG-2 PS pack header (14 bytes, 0 stuffing)
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x01;
    pack[pos++] = 0xBA;
    pack[pos++] = 0x44;
    pos += 9;
    pack[13] = 0xF8; // stuffing length 0

    // Video PES (stream 0xE0, PTS only, 32-byte payload)
    constexpr size_t kAvcPayloadSize = 32;
    const uint16_t video_packet_len = static_cast<uint16_t>(3u + 5u + kAvcPayloadSize);
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x01;
    pack[pos++] = 0xE0;
    WriteBe16(&pack[pos], video_packet_len);
    pos += 2;
    pack[pos++] = 0x80;
    pack[pos++] = 0x80; // PTS present
    pack[pos++] = 5;
    EncodePesPts(&pack[pos], pts);
    pos += 5;
    std::memset(&pack[pos], avc_fill, kAvcPayloadSize);
    pos += kAvcPayloadSize;

    // Audio PES (private_stream_1 0xBD, substream 0x00, 4-byte PSMF ATRAC subheader, 64-byte payload)
    constexpr size_t kAtracPayloadSize = 64;
    const uint16_t audio_packet_len = static_cast<uint16_t>(3u + 5u + 4u + kAtracPayloadSize);
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x01;
    pack[pos++] = 0xBD;
    WriteBe16(&pack[pos], audio_packet_len);
    pos += 2;
    pack[pos++] = 0x80;
    pack[pos++] = 0x80; // PTS present
    pack[pos++] = 5;
    EncodePesPts(&pack[pos], pts);
    pos += 5;
    pack[pos++] = 0x00; // substream 0
    pack[pos++] = 0x01;
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    std::memset(&pack[pos], atrac_fill, kAtracPayloadSize);
    pos += kAtracPayloadSize;

    // Padding PES filling the remainder of the 2048-byte sector.
    const size_t pad_rem = pack.size() - pos;
    TEST_ASSERT(pad_rem >= 6u);
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x01;
    pack[pos++] = 0xBE;
    WriteBe16(&pack[pos], static_cast<uint16_t>(pad_rem - 6u));
}

void BuildAudAndAtracPsPack(std::array<uint8_t, 2048>& pack, uint64_t pts)
{
    pack.fill(0);
    size_t pos = 0;

    // MPEG-2 PS pack header (14 bytes, 0 stuffing)
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x01;
    pack[pos++] = 0xBA;
    pack[pos++] = 0x44;
    pos += 9;
    pack[13] = 0xF8;

    // Video PES containing three H.264 Access Units delimited by 00 00 00 01 09.
    const std::array<uint8_t, 54> avc_es = {
        0x00, 0x00, 0x00, 0x01, 0x09, 0x10, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, // AU0: 16 B
        0x00, 0x00, 0x00, 0x01, 0x09, 0x30, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, // AU1: 18 B
        0x00, 0x00, 0x00, 0x01, 0x09, 0x30, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33 // AU2: 20 B
    };
    const uint16_t video_packet_len = static_cast<uint16_t>(3u + 5u + avc_es.size());
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x01;
    pack[pos++] = 0xE0;
    WriteBe16(&pack[pos], video_packet_len);
    pos += 2;
    pack[pos++] = 0x80;
    pack[pos++] = 0x80;
    pack[pos++] = 5;
    EncodePesPts(&pack[pos], pts);
    pos += 5;
    std::memcpy(&pack[pos], avc_es.data(), avc_es.size());
    pos += avc_es.size();

    // Audio PES containing two 24-byte PSP ATRAC3+ AUs (8-byte 0x0FD0 header + 16-byte frame).
    constexpr size_t kFrameBytes = 24;
    constexpr size_t kTotalAudioEs = kFrameBytes * 2u;
    const uint16_t audio_packet_len = static_cast<uint16_t>(3u + 5u + 4u + kTotalAudioEs);
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x01;
    pack[pos++] = 0xBD;
    WriteBe16(&pack[pos], audio_packet_len);
    pos += 2;
    pack[pos++] = 0x80;
    pack[pos++] = 0x80;
    pack[pos++] = 5;
    EncodePesPts(&pack[pos], pts);
    pos += 5;
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x00; // first AU starts at offset 0
    for (size_t f = 0; f < 2u; ++f)
    {
        pack[pos++] = 0x0F;
        pack[pos++] = 0xD0;
        pack[pos++] = 0x00;
        pack[pos++] = 0x01; // ((1) * 8) + 8 = 16 body bytes -> 24 bytes total
        pack[pos++] = 0x00;
        pack[pos++] = 0x00;
        pack[pos++] = 0x00;
        pack[pos++] = 0x00;
        std::memset(&pack[pos], static_cast<int>(0x60u + f), 16u);
        pos += 16u;
    }

    const size_t pad_rem = pack.size() - pos;
    TEST_ASSERT(pad_rem >= 6u);
    pack[pos++] = 0x00;
    pack[pos++] = 0x00;
    pack[pos++] = 0x01;
    pack[pos++] = 0xBE;
    WriteBe16(&pack[pos], static_cast<uint16_t>(pad_rem - 6u));
}

constexpr uint32_t kGuestMainEntry = 0x08802000u;
constexpr uint32_t kGuestRbCallbackEntry = 0x08802100u;

constexpr uint32_t kPsmfHdrAddr = 0x08900000u;
constexpr uint32_t kOutAddr = 0x08901000u;
constexpr uint32_t kRbStructAddr = 0x08902000u;
constexpr uint32_t kRbDataAddr = 0x08910000u;
constexpr uint32_t kMpegPtrAddr = 0x08920000u;
constexpr uint32_t kMpegWorkAddr = 0x08930000u;
constexpr uint32_t kAvcAuAddr = 0x08940000u;
constexpr uint32_t kAtracAuAddr = 0x08940100u;
constexpr uint32_t kAttrAddr = 0x08940200u;
constexpr uint32_t kInitAddr = 0x08940204u;
constexpr uint32_t kStatusAddr = 0x08940208u;
constexpr uint32_t kAtracEsAddr = 0x08950000u;
constexpr uint32_t kAtracPcmAddr = 0x08952000u;
constexpr uint32_t kAvcFbPtrAddr = 0x08960000u;
constexpr uint32_t kAvcFbAddr = 0x08A00000u;

constexpr uint32_t kCallbackCookie = 0x12345678u;
bool g_rb_callback_ran = false;
int32_t g_rb_callback_mode = 0;

void MockCallGuest(uint32_t entry)
{
    auto& mem = repsp::runtime::GetGuestMemory();

    if (entry == kGuestRbCallbackEntry)
    {
        g_rb_callback_ran = true;
        const uint32_t data_addr = g_cpu.gpr[4];
        const int32_t num_packets = static_cast<int32_t>(g_cpu.gpr[5]);
        const uint32_t callback_data = g_cpu.gpr[6];

        TEST_ASSERT(data_addr == kRbDataAddr);
        TEST_ASSERT(num_packets >= 1);
        TEST_ASSERT(callback_data == kCallbackCookie);

        // Verify blocking kernel wait succeeds from inside the callback's thread context.
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceKernelDelayThread", 100u)) == 0);

        if (g_rb_callback_mode == 0)
        {
            std::array<uint8_t, 2048> pack{};
            BuildSyntheticPsPack(pack, 3003ull, 0xA5u, 0x5Au);
            mem.WriteBlock(data_addr, pack.data(), pack.size());
            g_cpu.gpr[2] = 1u;
            return;
        }
        if (g_rb_callback_mode == 2)
        {
            std::array<uint8_t, 2048> pack{};
            BuildAudAndAtracPsPack(pack, 90000ull);
            mem.WriteBlock(data_addr, pack.data(), pack.size());
            g_cpu.gpr[2] = 1u;
            return;
        }

        // EOF mode returns 0 packets.
        g_cpu.gpr[2] = 0u;
        return;
    }

    if (entry == kGuestMainEntry)
    {
        std::array<uint8_t, 2048> hdr{};
        BuildSyntheticPsmfHeader(hdr);
        mem.WriteBlock(kPsmfHdrAddr, hdr.data(), hdr.size());

        // 1. Init + memory queries
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegInit")) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegQueryMemSize", 0u)) == repsp::hle::mpeg::kMpegMemSize);
        const int32_t rb_mem = static_cast<int32_t>(HleCall("sceMpegRingbufferQueryMemSize", 4u));
        TEST_ASSERT(rb_mem == 4 * (repsp::hle::mpeg::kMpegPacketSize + repsp::hle::mpeg::kMpegRingbufferPacketStructSize));

        // 2. Ringbuffer Construct + Create
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegRingbufferConstruct",
                                                 kRbStructAddr,
                                                 4u,
                                                 kRbDataAddr,
                                                 static_cast<uint32_t>(rb_mem),
                                                 kGuestRbCallbackEntry,
                                                 kCallbackCookie)) == 0);

        g_cpu.gpr[8] = 512u; // t0 = frame_width
        g_cpu.gpr[9] = 0u;   // t1 = mode
        mem.WriteU32(g_cpu.gpr[29], 0u); // stack arg6 (ddr_top)
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegCreate",
                                                 kMpegPtrAddr,
                                                 kMpegWorkAddr,
                                                 static_cast<uint32_t>(repsp::hle::mpeg::kMpegMemSize),
                                                 kRbStructAddr,
                                                 512u,
                                                 0u)) == 0);
        TEST_ASSERT(mem.ReadU32(kMpegPtrAddr) == kMpegWorkAddr);

        // 3. Stream offset & size (note: 2-arg sceMpegQueryStreamSize)
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegQueryStreamOffset", kMpegPtrAddr, kPsmfHdrAddr, kOutAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kOutAddr) == 0x800u);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegQueryStreamSize", kPsmfHdrAddr, kOutAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kOutAddr) == 2048u);

        // 4. Register streams & buffers
        const uint32_t avc_stream = HleCall("sceMpegRegistStream", kMpegPtrAddr, 0u, 0u);
        const uint32_t atrac_stream = HleCall("sceMpegRegistStream", kMpegPtrAddr, 1u, 0u);
        TEST_ASSERT(static_cast<int32_t>(avc_stream) >= 0);
        TEST_ASSERT(static_cast<int32_t>(atrac_stream) >= 0);

        const uint32_t es_buf = HleCall("sceMpegMallocAvcEsBuf", kMpegPtrAddr);
        TEST_ASSERT(es_buf != 0u);

        uint32_t es_sz_addr = kOutAddr;
        uint32_t out_sz_addr = kOutAddr + 4u;
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegQueryAtracEsSize", kMpegPtrAddr, es_sz_addr, out_sz_addr)) == 0);
        TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(es_sz_addr)) == repsp::hle::mpeg::kMpegAtracEsSize);
        TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(out_sz_addr)) == repsp::hle::mpeg::kMpegAtracOutSize);

        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegInitAu", kMpegPtrAddr, es_buf, kAvcAuAddr)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegInitAu", kMpegPtrAddr, kAtracEsAddr, kAtracAuAddr)) == 0);

        // 5. AvailableSize + Put with callee-saved register preservation check
        const int32_t avail = static_cast<int32_t>(HleCall("sceMpegRingbufferAvailableSize", kRbStructAddr));
        TEST_ASSERT(avail == 4);

        for (int reg = 16; reg <= 23; ++reg)
        {
            g_cpu.gpr[reg] = 0xA0000000u + static_cast<uint32_t>(reg);
        }
        g_cpu.gpr[30] = 0xF00DF00Du;
        const uint32_t saved_sp = g_cpu.gpr[29];
        const uint32_t saved_ra = g_cpu.gpr[31];

        g_rb_callback_ran = false;
        g_rb_callback_mode = 0;
        const int32_t added = static_cast<int32_t>(HleCall("sceMpegRingbufferPut",
                                                           kRbStructAddr,
                                                           2u,
                                                           static_cast<uint32_t>(avail)));
        TEST_ASSERT(added == 1);
        TEST_ASSERT(g_rb_callback_ran);

        for (int reg = 16; reg <= 23; ++reg)
        {
            TEST_ASSERT(g_cpu.gpr[reg] == 0xA0000000u + static_cast<uint32_t>(reg));
        }
        TEST_ASSERT(g_cpu.gpr[30] == 0xF00DF00Du);
        TEST_ASSERT(g_cpu.gpr[29] == saved_sp);
        TEST_ASSERT(g_cpu.gpr[31] == saved_ra);

        // 6. Retrieve AVC and ATRAC AUs & verify PTS at +0x04
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegGetAvcAu", kMpegPtrAddr, avc_stream, kAvcAuAddr, kAttrAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x00u) == 0u);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x04u) == 3003u);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x14u) == 32u);
        TEST_ASSERT(HleCall("sceMpegGetAvcAu", kMpegPtrAddr, avc_stream, kAvcAuAddr, kAttrAddr) ==
                    static_cast<uint32_t>(repsp::hle::kSceMpegErrorNoData));

        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegGetAtracAu", kMpegPtrAddr, atrac_stream, kAtracAuAddr, kAttrAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kAtracAuAddr + 0x00u) == 0u);
        TEST_ASSERT(mem.ReadU32(kAtracAuAddr + 0x04u) == 3003u);
        TEST_ASSERT(mem.ReadU32(kAtracAuAddr + 0x14u) == 64u);
        TEST_ASSERT(mem.ReadU8(kAtracEsAddr) == 0x5Au);
        TEST_ASSERT(HleCall("sceMpegGetAtracAu", kMpegPtrAddr, atrac_stream, kAtracAuAddr, kAttrAddr) ==
                    static_cast<uint32_t>(repsp::hle::kSceMpegErrorNoData));

        // 7. Decode AVC (indirect void** buf -> opaque black 8888, *init = 1)
        const size_t fb_bytes = 512u * 272u * 4u;
        std::vector<uint8_t> dirty_fb(fb_bytes, 0xAAu);
        mem.WriteBlock(kAvcFbAddr, dirty_fb.data(), dirty_fb.size());
        mem.WriteU32(kAvcFbPtrAddr, kAvcFbAddr);
        mem.WriteU32(kInitAddr, 0u);

        g_cpu.gpr[8] = kInitAddr; // arg4 (*init)
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegAvcDecode",
                                                 kMpegPtrAddr,
                                                 kAvcAuAddr,
                                                 512u,
                                                 kAvcFbPtrAddr,
                                                 kInitAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kInitAddr) == 1u);
        TEST_ASSERT(mem.ReadU32(kAvcFbAddr) == 0xFF000000u);
        TEST_ASSERT(mem.ReadU32(kAvcFbAddr + static_cast<uint32_t>(fb_bytes - 4u)) == 0xFF000000u);

        // 8. Decode ATRAC (silence -> zeroes)
        std::vector<uint8_t> dirty_pcm(repsp::hle::mpeg::kMpegAtracOutSize, 0x55u);
        mem.WriteBlock(kAtracPcmAddr, dirty_pcm.data(), dirty_pcm.size());
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegAtracDecode",
                                                 kMpegPtrAddr,
                                                 kAtracAuAddr,
                                                 kAtracPcmAddr,
                                                 1u)) == 0);
        std::vector<uint8_t> out_pcm(repsp::hle::mpeg::kMpegAtracOutSize, 0xFFu);
        mem.ReadBlock(kAtracPcmAddr, out_pcm.data(), out_pcm.size());
        for (uint8_t b : out_pcm)
        {
            TEST_ASSERT(b == 0u);
        }

        // 9. Flush, Stop, EOF callback, Teardown
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegAvcDecodeFlush", kMpegPtrAddr)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegAvcDecodeStop", kMpegPtrAddr, 512u, kAvcFbPtrAddr, kStatusAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kStatusAddr) == 0u);

        g_rb_callback_mode = 1;
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegRingbufferPut", kRbStructAddr, 1u, 4u)) == 0);

        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegFreeAvcEsBuf", kMpegPtrAddr, es_buf)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegUnRegistStream", kMpegPtrAddr, avc_stream)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegUnRegistStream", kMpegPtrAddr, atrac_stream)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegDelete", kMpegPtrAddr)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegRingbufferDestruct", kRbStructAddr)) == 0);

        // 10. Verify H.264 AUD (00 00 00 01 09) & PSP ATRAC (0x0FD0) multi-AU splitting in one PES
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegRingbufferConstruct",
                                                 kRbStructAddr,
                                                 4u,
                                                 kRbDataAddr,
                                                 static_cast<uint32_t>(rb_mem),
                                                 kGuestRbCallbackEntry,
                                                 kCallbackCookie)) == 0);
        g_cpu.gpr[8] = 512u;
        g_cpu.gpr[9] = 0u;
        mem.WriteU32(g_cpu.gpr[29], 0u);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegCreate",
                                                 kMpegPtrAddr,
                                                 kMpegWorkAddr,
                                                 static_cast<uint32_t>(repsp::hle::mpeg::kMpegMemSize),
                                                 kRbStructAddr,
                                                 512u,
                                                 0u)) == 0);
        const uint32_t avc2 = HleCall("sceMpegRegistStream", kMpegPtrAddr, 0u, 0u);
        const uint32_t atrac2 = HleCall("sceMpegRegistStream", kMpegPtrAddr, 1u, 0u);
        const uint32_t es2 = HleCall("sceMpegMallocAvcEsBuf", kMpegPtrAddr);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegInitAu", kMpegPtrAddr, es2, kAvcAuAddr)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegInitAu", kMpegPtrAddr, kAtracEsAddr, kAtracAuAddr)) == 0);

        g_rb_callback_mode = 2;
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegRingbufferPut", kRbStructAddr, 1u, 4u)) == 1);

        // Expect 2 completed AVC AUs (16B @ 90000, 18B @ 93003); 3rd stays buffered until EOF.
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegGetAvcAu", kMpegPtrAddr, avc2, kAvcAuAddr, kAttrAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x04u) == 90000u);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x14u) == 16u);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegGetAvcAu", kMpegPtrAddr, avc2, kAvcAuAddr, kAttrAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x04u) == 93003u);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x14u) == 18u);
        TEST_ASSERT(HleCall("sceMpegGetAvcAu", kMpegPtrAddr, avc2, kAvcAuAddr, kAttrAddr) ==
                    static_cast<uint32_t>(repsp::hle::kSceMpegErrorNoData));

        // Expect 2 ATRAC AUs (24B @ 90000, 24B @ 94180).
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegGetAtracAu", kMpegPtrAddr, atrac2, kAtracAuAddr, kAttrAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kAtracAuAddr + 0x04u) == 90000u);
        TEST_ASSERT(mem.ReadU32(kAtracAuAddr + 0x14u) == 24u);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegGetAtracAu", kMpegPtrAddr, atrac2, kAtracAuAddr, kAttrAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kAtracAuAddr + 0x04u) == 94180u);
        TEST_ASSERT(mem.ReadU32(kAtracAuAddr + 0x14u) == 24u);

        // EOF callback flushes the 3rd AVC AU (20B @ 96006).
        g_rb_callback_mode = 1;
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegRingbufferPut", kRbStructAddr, 1u, 4u)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegGetAvcAu", kMpegPtrAddr, avc2, kAvcAuAddr, kAttrAddr)) == 0);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x04u) == 96006u);
        TEST_ASSERT(mem.ReadU32(kAvcAuAddr + 0x14u) == 20u);

        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegFreeAvcEsBuf", kMpegPtrAddr, es2)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegUnRegistStream", kMpegPtrAddr, avc2)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegUnRegistStream", kMpegPtrAddr, atrac2)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegDelete", kMpegPtrAddr)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegRingbufferDestruct", kRbStructAddr)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceMpegFinish")) == 0);

        g_cpu.gpr[2] = 0u;
        return;
    }

    TEST_ASSERT(false);
}

const repsp_guest_cpu_ops g_mock_ops = {
    sizeof(MockCpuState),
    MockSave,
    MockRestore,
    MockGetGpr,
    MockSetGpr,
    MockCallGuest,
    MockGetFpr,
    MockSetFpr
};

void TestPsmfParser()
{
    std::array<uint8_t, 64> too_short{};
    repsp::hle::mpeg::PsmfHeaderInfo info{};
    TEST_ASSERT(!repsp::hle::mpeg::ParsePsmfHeader(too_short, info));

    std::array<uint8_t, 2048> hdr{};
    BuildSyntheticPsmfHeader(hdr);
    TEST_ASSERT(repsp::hle::mpeg::ParsePsmfHeader(hdr, info));
    TEST_ASSERT(info.valid);
    TEST_ASSERT(std::strcmp(info.version, "0014") == 0);
    TEST_ASSERT(info.stream_offset == 0x800u);
    TEST_ASSERT(info.stream_size == 2048u);
    TEST_ASSERT(info.first_pts == 90000ull);
    TEST_ASSERT(info.last_pts == 900000ull);
    TEST_ASSERT(info.stream_count == 2u);
    TEST_ASSERT(info.width == 480u);
    TEST_ASSERT(info.height == 272u);

    hdr[6] = '9';
    hdr[7] = '9';
    TEST_ASSERT(!repsp::hle::mpeg::ParsePsmfHeader(hdr, info));
}

void TestBlankAvcDecoder()
{
    repsp::codec::BlankAvcDecoder dec;
    TEST_ASSERT(std::strcmp(dec.Name(), "blank") == 0);

    std::array<uint8_t, 16> out{};
    TEST_ASSERT(dec.DecodeAu({}, 2u, 2u, repsp::codec::kAvcPixelFormat8888, out));
    for (size_t i = 0; i < 4; ++i)
    {
        const uint32_t px = static_cast<uint32_t>(out[i * 4]) |
                            (static_cast<uint32_t>(out[i * 4 + 1]) << 8) |
                            (static_cast<uint32_t>(out[i * 4 + 2]) << 16) |
                            (static_cast<uint32_t>(out[i * 4 + 3]) << 24);
        TEST_ASSERT(px == 0xFF000000u);
    }

    std::array<uint8_t, 8> out16{};
    TEST_ASSERT(dec.DecodeAu({}, 2u, 2u, repsp::codec::kAvcPixelFormat5650, out16));
    for (size_t i = 0; i < 4; ++i)
    {
        TEST_ASSERT(out16[i * 2] == 0x00u && out16[i * 2 + 1] == 0x00u);
    }

    TEST_ASSERT(dec.DecodeAu({}, 2u, 2u, repsp::codec::kAvcPixelFormat5551, out16));
    for (size_t i = 0; i < 4; ++i)
    {
        const uint16_t px = static_cast<uint16_t>(out16[i * 2]) |
                            (static_cast<uint16_t>(out16[i * 2 + 1]) << 8);
        TEST_ASSERT(px == 0x8000u);
    }

    TEST_ASSERT(dec.DecodeAu({}, 2u, 2u, repsp::codec::kAvcPixelFormat4444, out16));
    for (size_t i = 0; i < 4; ++i)
    {
        const uint16_t px = static_cast<uint16_t>(out16[i * 2]) |
                            (static_cast<uint16_t>(out16[i * 2 + 1]) << 8);
        TEST_ASSERT(px == 0xF000u);
    }

    std::array<uint8_t, 4> small_out{};
    TEST_ASSERT(!dec.DecodeAu({}, 2u, 2u, repsp::codec::kAvcPixelFormat8888, small_out));
}

} // namespace

int main()
{
    using namespace repsp::runtime;
    using namespace repsp::hle;

    TestPsmfParser();
    TestBlankAvcDecoder();

    TEST_ASSERT(GetGuestMemory().Initialize());
    repsp_recomp_register_cpu(&g_mock_ops);
    kernel::Register(kernel::KernelConfig{});
    mpeg::Register();

    const int32_t tid = GetScheduler().CreateThread("mpeg_main", kGuestMainEntry, 0x20, 0x8000, 0, 0);
    TEST_ASSERT(tid > 0);
    GetScheduler().Run(tid);

    const auto stats = mpeg::GetStats();
    TEST_ASSERT(stats.init_calls == 1);
    TEST_ASSERT(stats.finish_calls == 1);
    TEST_ASSERT(stats.create_calls == 2);
    TEST_ASSERT(stats.delete_calls == 2);
    TEST_ASSERT(stats.ringbuffer_put_calls == 4);
    TEST_ASSERT(stats.ringbuffer_packets_added == 2);
    TEST_ASSERT(stats.psmf_headers_parsed == 1);
    TEST_ASSERT(stats.streams_registered == 4);
    TEST_ASSERT(stats.avc_aus_returned == 4);
    TEST_ASSERT(stats.atrac_aus_returned == 3);
    TEST_ASSERT(stats.avc_frames_decoded == 1);
    TEST_ASSERT(stats.atrac_decode_calls == 1);

    const std::string report = mpeg::FormatStats();
    TEST_ASSERT(report.find("AVC decode: blank") != std::string::npos);
    TEST_ASSERT(report.find("ATRAC decode: silent") != std::string::npos);

    mpeg::Shutdown();
    GetScheduler().Reset();
    std::printf("mpeg_probe passed.\n");
    return 0;
}
