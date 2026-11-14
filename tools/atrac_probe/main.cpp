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
                                       bool atrac3plus)
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
    PushU32(out, 69);

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

    // data chunk
    const uint32_t data_bytes = block_align * num_frames;
    PushFourCc(out, "data");
    PushU32(out, data_bytes);
    for (uint32_t i = 0; i < data_bytes; ++i)
    {
        out.push_back(static_cast<uint8_t>((i * 37u) & 0xFFu));
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

    const std::vector<uint8_t> at3_data = BuildSyntheticAt3(192, 3, 3072, 1024, 2048, false);
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
    TEST_ASSERT(static_cast<int32_t>(mem.ReadU32(kVars + 8u)) == 2048);

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

    Shutdown();
    mem.Shutdown();

    std::printf("[PASS] atrac_probe: 4/4 checks passed\n");
    return 0;
}
