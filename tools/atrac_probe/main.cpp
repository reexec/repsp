#include "repsp/hle/atrac.h"
#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define TEST_ASSERT(cond)                                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(cond))                                                                   \
        {                                                                              \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);     \
            return 1;                                                                  \
        }                                                                              \
    } while (false)

namespace
{

struct MockCpu
{
    uint32_t gpr[32] = {};
};

MockCpu g_cpu;

void MockSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void MockRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t MockGetGpr(int idx) { return (idx > 0 && idx < 32) ? g_cpu.gpr[idx] : 0; }
void MockSetGpr(int idx, uint32_t val) { if (idx > 0 && idx < 32) g_cpu.gpr[idx] = val; }
uint32_t MockGetFpr(int) { return 0; }
void MockSetFpr(int, uint32_t) {}
void MockCallGuest(uint32_t) {}

int32_t CallNid5(uint32_t nid,
                 uint32_t a0 = 0,
                 uint32_t a1 = 0,
                 uint32_t a2 = 0,
                 uint32_t a3 = 0,
                 uint32_t t0 = 0)
{
    MockSetGpr(4, a0);
    MockSetGpr(5, a1);
    MockSetGpr(6, a2);
    MockSetGpr(7, a3);
    MockSetGpr(8, t0);
    repsp_recomp_hle_call(nid);
    return static_cast<int32_t>(MockGetGpr(2));
}

constexpr uint32_t kNidSetDataAndGetId = 0x7A20E7AFu;
constexpr uint32_t kNidDecodeData = 0x6A8C3CD5u;
constexpr uint32_t kNidGetRemainFrame = 0x9AE849A7u;
constexpr uint32_t kNidGetStreamDataInfo = 0x5D268707u;
constexpr uint32_t kNidAddStreamData = 0x7DB31251u;
constexpr uint32_t kNidSetLoopNum = 0x868120B5u;
constexpr uint32_t kNidReleaseAtracId = 0x61EB33F5u;
constexpr uint32_t kNidGetSoundSample = 0xA2BBA8BEu;
constexpr uint32_t kNidResetPlayPosition = 0x644E5607u;
constexpr uint32_t kNidGetBufferInfoForReseting = 0xCA3CA3D2u;
constexpr uint32_t kNidGetInternalErrorInfo = 0xE88F759Bu;

void PushU16(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void PushU32(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void PushFourCc(std::vector<uint8_t>& buf, const char* cc)
{
    buf.insert(buf.end(), cc, cc + 4);
}

std::vector<uint8_t> BuildSyntheticAt3(uint16_t block_align,
                                       uint32_t num_frames,
                                       uint32_t end_sample,
                                       int32_t loop_start,
                                       int32_t loop_end,
                                       bool atrac3plus,
                                       uint32_t encoder_delay = 0)
{
    std::vector<uint8_t> out;
    PushFourCc(out, "RIFF");
    PushU32(out, 0); // patched at end
    PushFourCc(out, "WAVE");

    // fmt chunk
    PushFourCc(out, "fmt ");
    if (atrac3plus)
    {
        PushU32(out, 52);
        PushU16(out, repsp::hle::atrac::kWaveFormatExtensible);
        PushU16(out, 2);
        PushU32(out, 44100);
        PushU32(out, 44100 * block_align / 2048);
        PushU16(out, block_align);
        PushU16(out, 0);
        PushU16(out, 34); // cbSize
        PushU16(out, 2048);
        PushU32(out, 3);
        static constexpr std::array<uint8_t, 16> kGuid = {
            0xE9, 0x23, 0xAA, 0xBF, 0xCB, 0x58, 0x44, 0x41,
            0xA1, 0x19, 0xFF, 0xFA, 0x01, 0xE4, 0xCE, 0x62,
        };
        out.insert(out.end(), kGuid.begin(), kGuid.end());
        for (int i = 0; i < 12; ++i)
        {
            out.push_back(0);
        }
    }
    else
    {
        PushU32(out, 16);
        PushU16(out, repsp::hle::atrac::kAtrac3FormatTag);
        PushU16(out, 2);
        PushU32(out, 44100);
        PushU32(out, 44100 * block_align / 1024);
        PushU16(out, block_align);
        PushU16(out, 0);
    }

    // fact chunk
    PushFourCc(out, "fact");
    PushU32(out, 8);
    PushU32(out, end_sample);
    PushU32(out, encoder_delay);

    if (loop_start >= 0 && loop_end >= loop_start)
    {
        PushFourCc(out, "smpl");
        PushU32(out, 36 + 24);
        for (int i = 0; i < 7; ++i)
        {
            PushU32(out, 0);
        }
        PushU32(out, 1); // cSampleLoops
        PushU32(out, 0); // cbSamplerData
        PushU32(out, 0); // dwIdentifier
        PushU32(out, 0); // dwType
        PushU32(out, static_cast<uint32_t>(loop_start));
        PushU32(out, static_cast<uint32_t>(loop_end));
        PushU32(out, 0);
        PushU32(out, 0);
    }

    // data chunk: tag each frame's first 2 bytes with its LE frame index for test decoders.
    const uint32_t data_bytes = block_align * num_frames;
    PushFourCc(out, "data");
    PushU32(out, data_bytes);
    for (uint32_t f = 0; f < num_frames; ++f)
    {
        for (uint32_t b = 0; b < block_align; ++b)
        {
            if (b == 0)
            {
                out.push_back(static_cast<uint8_t>(f & 0xFFu));
            }
            else if (b == 1)
            {
                out.push_back(static_cast<uint8_t>((f >> 8) & 0xFFu));
            }
            else
            {
                out.push_back(static_cast<uint8_t>(((f * block_align + b) * 37u) & 0xFFu));
            }
        }
    }

    const uint32_t riff_sz = static_cast<uint32_t>(out.size() - 8);
    out[4] = static_cast<uint8_t>(riff_sz & 0xFFu);
    out[5] = static_cast<uint8_t>((riff_sz >> 8) & 0xFFu);
    out[6] = static_cast<uint8_t>((riff_sz >> 16) & 0xFFu);
    out[7] = static_cast<uint8_t>((riff_sz >> 24) & 0xFFu);
    return out;
}

class MockPatternDecoder final : public repsp::hle::atrac::AtracDecoder
{
public:
    bool DecodeFrame(const repsp::hle::atrac::AtracFrameInfo& info,
                     const uint8_t* frame_bytes,
                     size_t frame_size,
                     int16_t* pcm_out,
                     size_t out_samples) override
    {
        (void)frame_bytes;
        (void)frame_size;
        std::fill(pcm_out, pcm_out + out_samples * info.channels, int16_t{ 0x1234 });
        return true;
    }
    const char* Name() const override { return "mock"; }
};

class IndexedFrameDecoder final : public repsp::hle::atrac::AtracDecoder
{
public:
    bool DecodeFrame(const repsp::hle::atrac::AtracFrameInfo& info,
                     const uint8_t* frame_bytes,
                     size_t frame_size,
                     int16_t* pcm_out,
                     size_t out_samples) override
    {
        if (frame_size < 2 || info.channels != 2)
        {
            return false;
        }
        const uint16_t frame_idx =
            static_cast<uint16_t>(frame_bytes[0]) | (static_cast<uint16_t>(frame_bytes[1]) << 8);
        for (size_t i = 0; i < out_samples; ++i)
        {
            pcm_out[i * 2u] = static_cast<int16_t>(frame_idx);
            pcm_out[i * 2u + 1u] = static_cast<int16_t>(i);
        }
        decoded_frames += 1;
        return true;
    }
    void* CreateStreamContext() override { return this; }
    void ResetStreamContext(void*) override { resets += 1; }
    void DestroyStreamContext(void*) override {}
    const char* Name() const override { return "indexed"; }

    uint32_t decoded_frames = 0;
    uint32_t resets = 0;
};

} // namespace

int main()
{
    using namespace repsp::hle;
    using namespace repsp::hle::atrac;

    auto& mem = repsp::runtime::GetGuestMemory();
    TEST_ASSERT(mem.Initialize());

    const repsp_guest_cpu_ops ops{
        sizeof(MockCpu), MockSave, MockRestore,
        MockGetGpr, MockSetGpr, MockCallGuest,
        MockGetFpr, MockSetFpr,
    };
    repsp_recomp_register_cpu(&ops);
    kernel::Register(kernel::KernelConfig{});
    Register();

    constexpr uint32_t kAt3Buf = 0x08960000u;
    constexpr uint32_t kStreamBuf = 0x08970000u;
    constexpr uint32_t kPcmOut = 0x08980000u;
    constexpr uint32_t kVars = 0x08990000u;

    // 1. Error checks and max handle capacity (6 handles).
    TEST_ASSERT(CallNid5(kNidSetDataAndGetId, kAt3Buf + 1u, 64u) == kSceAtracErrorNot4ByteAlignment);
    mem.WriteU32(kAt3Buf, 0x12345678u);
    TEST_ASSERT(CallNid5(kNidSetDataAndGetId, kAt3Buf, 64u) == kSceAtracErrorUnknownFormat);
    TEST_ASSERT(CallNid5(kNidGetRemainFrame, static_cast<uint32_t>(-1), kVars) ==
                kSceAtracErrorBadAtracId);

    const std::vector<uint8_t> at3_data = BuildSyntheticAt3(192, 3, 3072, 1024, 2047, false, 0);
    std::memcpy(mem.Translate(kAt3Buf), at3_data.data(), at3_data.size());

    std::array<int32_t, kAtracMaxIds> ids{};
    for (int i = 0; i < kAtracMaxIds; ++i)
    {
        ids[static_cast<size_t>(i)] =
            CallNid5(kNidSetDataAndGetId, kAt3Buf, static_cast<uint32_t>(at3_data.size()));
        TEST_ASSERT(ids[static_cast<size_t>(i)] >= 0);
    }
    TEST_ASSERT(CallNid5(kNidSetDataAndGetId, kAt3Buf, static_cast<uint32_t>(at3_data.size())) ==
                kSceAtracErrorNoAtracId);
    for (int i = 1; i < kAtracMaxIds; ++i)
    {
        TEST_ASSERT(CallNid5(kNidReleaseAtracId, static_cast<uint32_t>(ids[static_cast<size_t>(i)])) ==
                    0);
    }

    // 2. Full-memory AT3 decode, silence check, sound samples, loop, and reset.
    const uint32_t id = static_cast<uint32_t>(ids[0]);
    TEST_ASSERT(CallNid5(kNidGetRemainFrame, id, kVars) == 0);
    TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars)) == kAtracRemainAllInMemory);
    TEST_ASSERT(CallNid5(kNidAddStreamData, id, 64u) == kSceAtracErrorAllDataIsOnMemory);

    TEST_ASSERT(CallNid5(kNidGetSoundSample, id, kVars, kVars + 4u, kVars + 8u) == 0);
    TEST_ASSERT(mem.ReadU32(kVars) == 3072u);
    TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars + 4u)) == 1024);
    TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars + 8u)) == 2047);

    TEST_ASSERT(CallNid5(kNidGetInternalErrorInfo, id, kVars) == 0);
    TEST_ASSERT(mem.ReadU32(kVars) == 0u);

    std::memset(mem.Translate(kPcmOut), 0x5Au, 2048 * 4);
    for (int f = 0; f < 3; ++f)
    {
        TEST_ASSERT(CallNid5(kNidDecodeData, id, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == (f == 2 ? 1u : 0u));
        TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars + 8u)) == kAtracRemainAllInMemory);
        for (uint32_t i = 0; i < 1024u * 2u; ++i)
        {
            TEST_ASSERT(mem.ReadU16(kPcmOut + i * 2u) == 0u);
        }
    }
    TEST_ASSERT(CallNid5(kNidDecodeData, id, kPcmOut, kVars, kVars + 4u, kVars + 8u) ==
                kSceAtracErrorAllDataWasDecoded);

    TEST_ASSERT(CallNid5(kNidGetBufferInfoForReseting, id, 4000u, kVars + 16u) ==
                kSceAtracErrorBadSample);
    TEST_ASSERT(CallNid5(kNidGetBufferInfoForReseting, id, 0u, kVars + 16u) == 0);
    TEST_ASSERT(CallNid5(kNidResetPlayPosition, id, 0u, 0u, 0u) == 0);
    TEST_ASSERT(CallNid5(kNidSetLoopNum, id, 1u) == 0);

    // Frames: [0..1024], [1024..2048 -> loop to 1024], [1024..2048], [2048..3072 -> end=1].
    int ended_at = -1;
    for (int f = 0; f < 4; ++f)
    {
        TEST_ASSERT(CallNid5(kNidDecodeData, id, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        if (mem.ReadU32(kVars + 4u) == 1u)
        {
            ended_at = f;
        }
    }
    TEST_ASSERT(ended_at == 3);
    TEST_ASSERT(CallNid5(kNidReleaseAtracId, id) == 0);

    // 3. Streaming AT3+ (0xFFFE Extensible GUID, 2048 spf): GetStreamDataInfo -> AddStreamData.
    const std::vector<uint8_t> at3p_data = BuildSyntheticAt3(384, 2, 4096, -1, -1, true);
    const uint32_t initial_load = static_cast<uint32_t>(at3p_data.size() - 384u);
    std::memcpy(mem.Translate(kStreamBuf), at3p_data.data(), initial_load);

    const int32_t sid = CallNid5(kNidSetDataAndGetId, kStreamBuf, initial_load);
    TEST_ASSERT(sid >= 0);
    const uint32_t suid = static_cast<uint32_t>(sid);

    TEST_ASSERT(CallNid5(kNidGetRemainFrame, suid, kVars) == 0);
    TEST_ASSERT(mem.ReadU32(kVars) == 1u);

    TEST_ASSERT(CallNid5(kNidDecodeData, suid, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
    TEST_ASSERT(mem.ReadU32(kVars) == 2048u);
    TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
    TEST_ASSERT(mem.ReadU32(kVars + 8u) == 0u);

    TEST_ASSERT(CallNid5(kNidGetStreamDataInfo, suid, kVars, kVars + 4u, kVars + 8u) == 0);
    const uint32_t write_ptr = mem.ReadU32(kVars);
    const uint32_t avail = mem.ReadU32(kVars + 4u);
    const uint32_t read_off = mem.ReadU32(kVars + 8u);
    TEST_ASSERT(avail == 384u);
    TEST_ASSERT(read_off == initial_load);

    TEST_ASSERT(CallNid5(kNidAddStreamData, suid, avail + 1u) == kSceAtracErrorAddDataIsTooBig);
    std::memcpy(mem.Translate(write_ptr), at3p_data.data() + read_off, avail);
    TEST_ASSERT(CallNid5(kNidAddStreamData, suid, avail) == 0);

    TEST_ASSERT(CallNid5(kNidDecodeData, suid, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
    TEST_ASSERT(mem.ReadU32(kVars) == 2048u);
    TEST_ASSERT(mem.ReadU32(kVars + 4u) == 1u);

    // 4. Replaceable AtracDecoder contract + silent decoder indicator in FormatStats().
    MockPatternDecoder mock;
    SetDecoder(&mock);
    const int32_t mid = CallNid5(kNidSetDataAndGetId, kAt3Buf, static_cast<uint32_t>(at3_data.size()));
    TEST_ASSERT(mid >= 0);
    TEST_ASSERT(CallNid5(kNidDecodeData, static_cast<uint32_t>(mid), kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
    TEST_ASSERT(static_cast<int16_t>(mem.ReadU16(kPcmOut)) == 0x1234);
    TEST_ASSERT(GetStats().decoder_name == "mock");
    TEST_ASSERT(CallNid5(kNidReleaseAtracId, static_cast<uint32_t>(mid)) == 0);

    SetDecoder(nullptr);
    TEST_ASSERT(FormatStats().find("ATRAC decode: silent") != std::string::npos);

    // Helper to read raw sample index encoded by IndexedFrameDecoder (L=frame, R=sample_in_frame).
    auto read_raw_sample = [&](uint32_t pcm_addr, uint32_t sample_idx, uint32_t spf) -> uint32_t
    {
        const uint32_t frame_idx = mem.ReadU16(pcm_addr + sample_idx * 4u);
        const uint32_t in_frame = mem.ReadU16(pcm_addr + sample_idx * 4u + 2u);
        return frame_idx * spf + in_frame;
    };

    // 5. Encoder delay skipping (delay=1561, spf=1024, endSample=2500) + exact end and short final block.
    {
        IndexedFrameDecoder idx_dec;
        SetDecoder(&idx_dec);

        constexpr uint32_t kDelay = 1561u;
        constexpr uint32_t kEndSample = 2500u;
        constexpr uint32_t kSpf = 1024u;
        const std::vector<uint8_t> delayed_data =
            BuildSyntheticAt3(192, 5, kEndSample, -1, -1, false, kDelay);
        std::memcpy(mem.Translate(kAt3Buf), delayed_data.data(), delayed_data.size());

        const int32_t did =
            CallNid5(kNidSetDataAndGetId, kAt3Buf, static_cast<uint32_t>(delayed_data.size()));
        TEST_ASSERT(did >= 0);
        const uint32_t duid = static_cast<uint32_t>(did);

        TEST_ASSERT(CallNid5(kNidGetSoundSample, duid, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == kEndSample);
        TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars + 4u)) == -1);
        TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars + 8u)) == -1);

        // Call 1: 1024 samples covering raw [1561..2584]; frames 0, 1, 2 decoded.
        TEST_ASSERT(CallNid5(kNidDecodeData, duid, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        TEST_ASSERT(idx_dec.decoded_frames == 3u);
        for (uint32_t i = 0; i < 1024u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, i, kSpf) == kDelay + i);
        }

        // Call 2: 1024 samples covering raw [2585..3608], continuous without gap.
        TEST_ASSERT(CallNid5(kNidDecodeData, duid, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        for (uint32_t i = 0; i < 1024u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, i, kSpf) == kDelay + 1024u + i);
        }

        // Call 3: short final block of 452 samples covering raw [3609..4060], end == 1.
        TEST_ASSERT(CallNid5(kNidDecodeData, duid, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 452u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 1u);
        for (uint32_t i = 0; i < 452u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, i, kSpf) == kDelay + 2048u + i);
        }
        TEST_ASSERT(CallNid5(kNidDecodeData, duid, kPcmOut, kVars, kVars + 4u, kVars + 8u) ==
                    kSceAtracErrorAllDataWasDecoded);
        TEST_ASSERT(CallNid5(kNidReleaseAtracId, duid) == 0);
    }

    // 6. Infinite loop (-1) with delay=1561: loop seam spliced inside a 1024-sample block.
    {
        IndexedFrameDecoder idx_dec;
        SetDecoder(&idx_dec);

        constexpr uint32_t kDelay = 1561u;
        constexpr uint32_t kEndSample = 2500u;
        constexpr uint32_t kSpf = 1024u;
        constexpr int32_t kLoopStart = 1561;
        constexpr int32_t kLoopEnd = 4060; // endSample + delay - 1
        const std::vector<uint8_t> loop_data =
            BuildSyntheticAt3(192, 5, kEndSample, kLoopStart, kLoopEnd, false, kDelay);
        std::memcpy(mem.Translate(kAt3Buf), loop_data.data(), loop_data.size());

        const int32_t lid =
            CallNid5(kNidSetDataAndGetId, kAt3Buf, static_cast<uint32_t>(loop_data.size()));
        TEST_ASSERT(lid >= 0);
        const uint32_t luid = static_cast<uint32_t>(lid);

        TEST_ASSERT(CallNid5(kNidGetSoundSample, luid, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == kEndSample);
        TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars + 4u)) == 0);
        TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars + 8u)) == 2499);
        TEST_ASSERT(CallNid5(kNidSetLoopNum, luid, static_cast<uint32_t>(-1)) == 0);

        // Calls 1..2: [1561..2584], [2585..3608]
        for (uint32_t call = 0; call < 2u; ++call)
        {
            TEST_ASSERT(CallNid5(kNidDecodeData, luid, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
            TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
            TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        }

        // Call 3: 452 samples [3609..4060] followed immediately by 572 samples [1561..2132].
        TEST_ASSERT(CallNid5(kNidDecodeData, luid, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        TEST_ASSERT(idx_dec.resets >= 1u);
        for (uint32_t i = 0; i < 452u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, i, kSpf) == 3609u + i);
        }
        for (uint32_t i = 0; i < 572u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, 452u + i, kSpf) == static_cast<uint32_t>(kLoopStart) + i);
        }

        // Call 4: continues seamlessly at [2133..3156].
        TEST_ASSERT(CallNid5(kNidDecodeData, luid, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        for (uint32_t i = 0; i < 1024u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, i, kSpf) == 2133u + i);
        }
        TEST_ASSERT(CallNid5(kNidReleaseAtracId, luid) == 0);
    }

    // 7. Streaming with delay=1561: shortage returns 0 samples without losing carry samples.
    {
        IndexedFrameDecoder idx_dec;
        SetDecoder(&idx_dec);

        constexpr uint32_t kDelay = 1561u;
        constexpr uint32_t kEndSample = 2500u;
        constexpr uint32_t kSpf = 1024u;
        constexpr uint16_t kBlockAlign = 192u;
        const std::vector<uint8_t> st_data =
            BuildSyntheticAt3(kBlockAlign, 5, kEndSample, -1, -1, false, kDelay);
        const uint32_t data_bytes = kBlockAlign * 5u;
        const uint32_t data_off = static_cast<uint32_t>(st_data.size()) - data_bytes;
        // Initially buffer only frames 0 and 1 (not enough to complete the first 1024-sample output).
        const uint32_t init_len = data_off + kBlockAlign * 2u;
        std::memcpy(mem.Translate(kStreamBuf), st_data.data(), init_len);

        const int32_t sid2 = CallNid5(kNidSetDataAndGetId, kStreamBuf, init_len);
        TEST_ASSERT(sid2 >= 0);
        const uint32_t suid2 = static_cast<uint32_t>(sid2);

        // Call 1: decodes frames 0 & 1 (487 carry samples), stalls on missing frame 2 -> returns 0 samples.
        TEST_ASSERT(CallNid5(kNidDecodeData, suid2, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 0u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        TEST_ASSERT(mem.ReadU32(kVars + 8u) == 0u);

        // Add frame 2 (192 bytes) and decode: must start from carry sample 1561 without losing anything.
        TEST_ASSERT(CallNid5(kNidGetStreamDataInfo, suid2, kVars, kVars + 4u, kVars + 8u) == 0);
        uint32_t wptr = mem.ReadU32(kVars);
        uint32_t wavail = mem.ReadU32(kVars + 4u);
        uint32_t roff = mem.ReadU32(kVars + 8u);
        TEST_ASSERT(wavail >= kBlockAlign);
        std::memcpy(mem.Translate(wptr), st_data.data() + roff, kBlockAlign);
        TEST_ASSERT(CallNid5(kNidAddStreamData, suid2, kBlockAlign) == 0);

        TEST_ASSERT(CallNid5(kNidDecodeData, suid2, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        for (uint32_t i = 0; i < 1024u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, i, kSpf) == kDelay + i);
        }

        // Next call stalls again (carry has 487 samples [2585..3071], needs frame 3).
        TEST_ASSERT(CallNid5(kNidDecodeData, suid2, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 0u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);

        // Add remaining frames (frames 3 & 4) respecting ring wrap-around, and finish cleanly.
        uint32_t to_feed = kBlockAlign * 2u;
        while (to_feed > 0)
        {
            TEST_ASSERT(CallNid5(kNidGetStreamDataInfo, suid2, kVars, kVars + 4u, kVars + 8u) == 0);
            wptr = mem.ReadU32(kVars);
            wavail = mem.ReadU32(kVars + 4u);
            roff = mem.ReadU32(kVars + 8u);
            TEST_ASSERT(wavail > 0);
            const uint32_t chunk = (wavail < to_feed) ? wavail : to_feed;
            std::memcpy(mem.Translate(wptr), st_data.data() + roff, chunk);
            TEST_ASSERT(CallNid5(kNidAddStreamData, suid2, chunk) == 0);
            to_feed -= chunk;
        }

        TEST_ASSERT(CallNid5(kNidDecodeData, suid2, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        for (uint32_t i = 0; i < 1024u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, i, kSpf) == kDelay + 1024u + i);
        }

        TEST_ASSERT(CallNid5(kNidDecodeData, suid2, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 452u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 1u);
        for (uint32_t i = 0; i < 452u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, i, kSpf) == kDelay + 2048u + i);
        }
        TEST_ASSERT(CallNid5(kNidReleaseAtracId, suid2) == 0);
    }

    // 8. SetStartOffsetMs(0 / 1000): skips on open, does not apply to loops or ResetPlayPosition.
    {
        IndexedFrameDecoder idx_dec;
        SetDecoder(&idx_dec);

        constexpr uint32_t kDelay = 1561u;
        constexpr uint32_t kEndSample = 46000u; // > 44100 (1000 ms at 44.1 kHz)
        constexpr uint32_t kSpf = 1024u;
        constexpr uint32_t kFrames = 50u;       // 51200 raw samples
        constexpr int32_t kLoopStart = 1561;
        constexpr int32_t kLoopEnd = static_cast<int32_t>(kEndSample + kDelay - 1u); // 47560
        const std::vector<uint8_t> off_data =
            BuildSyntheticAt3(192, kFrames, kEndSample, kLoopStart, kLoopEnd, false, kDelay);
        std::memcpy(mem.Translate(kAt3Buf), off_data.data(), off_data.size());

        // Offset 0 ms -> first sample is delay (1561).
        SetStartOffsetMs(0);
        TEST_ASSERT(GetStartOffsetMs() == 0);
        const int32_t id0 =
            CallNid5(kNidSetDataAndGetId, kAt3Buf, static_cast<uint32_t>(off_data.size()));
        TEST_ASSERT(id0 >= 0);
        TEST_ASSERT(CallNid5(kNidDecodeData, static_cast<uint32_t>(id0), kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(read_raw_sample(kPcmOut, 0, kSpf) == kDelay);
        TEST_ASSERT(CallNid5(kNidReleaseAtracId, static_cast<uint32_t>(id0)) == 0);

        // Offset +1000 ms (44100 samples at 44100 Hz) -> first sample is 1561 + 44100 = 45661.
        SetStartOffsetMs(1000);
        TEST_ASSERT(GetStartOffsetMs() == 1000);
        const int32_t id1 =
            CallNid5(kNidSetDataAndGetId, kAt3Buf, static_cast<uint32_t>(off_data.size()));
        TEST_ASSERT(id1 >= 0);
        const uint32_t uid1 = static_cast<uint32_t>(id1);
        TEST_ASSERT(CallNid5(kNidSetLoopNum, uid1, static_cast<uint32_t>(-1)) == 0);

        // Call 1: raw [45661..46684]
        TEST_ASSERT(CallNid5(kNidDecodeData, uid1, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(read_raw_sample(kPcmOut, 0, kSpf) == kDelay + 44100u);

        // Call 2: remaining 876 samples [46685..47560] then loop to kLoopStart (1561) with NO offset!
        TEST_ASSERT(CallNid5(kNidDecodeData, uid1, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(read_raw_sample(kPcmOut, 0, kSpf) == 46685u);
        TEST_ASSERT(read_raw_sample(kPcmOut, 875u, kSpf) == 47560u);
        TEST_ASSERT(read_raw_sample(kPcmOut, 876u, kSpf) == static_cast<uint32_t>(kLoopStart));

        // ResetPlayPosition(0) also ignores start_offset_ms -> resumes at delay (1561).
        TEST_ASSERT(CallNid5(kNidResetPlayPosition, uid1, 0u, 0u, 0u) == 0);
        TEST_ASSERT(CallNid5(kNidDecodeData, uid1, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(read_raw_sample(kPcmOut, 0, kSpf) == kDelay);

        TEST_ASSERT(CallNid5(kNidReleaseAtracId, uid1) == 0);

        // Offset -1000 ms (44100 leading silence samples at 44100 Hz), block size 1024 kept.
        SetStartOffsetMs(-1000);
        TEST_ASSERT(GetStartOffsetMs() == -1000);
        const int32_t id2 =
            CallNid5(kNidSetDataAndGetId, kAt3Buf, static_cast<uint32_t>(off_data.size()));
        TEST_ASSERT(id2 >= 0);
        const uint32_t uid2 = static_cast<uint32_t>(id2);

        // First 43 blocks (43 * 1024 = 44032 samples) are all silence (0).
        for (uint32_t blk = 0; blk < 43u; ++blk)
        {
            TEST_ASSERT(CallNid5(kNidDecodeData, uid2, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
            TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
            TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
            for (uint32_t i = 0; i < 1024u * 2u; ++i)
            {
                TEST_ASSERT(mem.ReadU16(kPcmOut + i * 2u) == 0u);
            }
        }
        // Block 44: 68 silence samples (44100 - 44032), then raw [1561..2516] (956 samples).
        TEST_ASSERT(CallNid5(kNidDecodeData, uid2, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(mem.ReadU32(kVars + 4u) == 0u);
        for (uint32_t i = 0; i < 68u * 2u; ++i)
        {
            TEST_ASSERT(mem.ReadU16(kPcmOut + i * 2u) == 0u);
        }
        for (uint32_t i = 0; i < 956u; ++i)
        {
            TEST_ASSERT(read_raw_sample(kPcmOut, 68u + i, kSpf) == kDelay + i);
        }
        // ResetPlayPosition(0) with negative offset ignores offset -> immediately starts at delay (1561).
        TEST_ASSERT(CallNid5(kNidResetPlayPosition, uid2, 0u, 0u, 0u) == 0);
        TEST_ASSERT(CallNid5(kNidDecodeData, uid2, kPcmOut, kVars, kVars + 4u, kVars + 8u) == 0);
        TEST_ASSERT(mem.ReadU32(kVars) == 1024u);
        TEST_ASSERT(read_raw_sample(kPcmOut, 0, kSpf) == kDelay);

        TEST_ASSERT(CallNid5(kNidReleaseAtracId, uid2) == 0);
        SetStartOffsetMs(0);
    }

    SetDecoder(nullptr);
    Shutdown();
    mem.Shutdown();

    std::printf("[PASS] atrac_probe: 8/8 checks passed\n");
    return 0;
}
