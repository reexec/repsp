#include "repsp/hle/audio.h"
#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/platform/platform.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

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

int32_t CallNid(uint32_t nid, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0)
{
    MockSetGpr(4, a0);
    MockSetGpr(5, a1);
    MockSetGpr(6, a2);
    MockSetGpr(7, a3);
    repsp_recomp_hle_call(nid);
    return static_cast<int32_t>(MockGetGpr(2));
}

constexpr uint32_t kNidChReserve = 0x5EC81C55u;
constexpr uint32_t kNidChRelease = 0x6FC46853u;
constexpr uint32_t kNidOutputBlocking = 0x136CAF51u;
constexpr uint32_t kNidOutputPannedBlocking = 0x13F592BCu;
constexpr uint32_t kNidGetRestLength = 0xB011922Fu;
constexpr uint32_t kNidSetDataLen = 0xCB2E439Eu;

constexpr uint32_t kGuestBuf0 = 0x08940000u;
constexpr uint32_t kGuestBuf1 = 0x08942000u;
constexpr uint32_t kThreadEntry = 0x08803000u;

int32_t g_live_ch = -1;
int32_t g_live_rest_after = -1;
int32_t g_live_calls_ok = 0;

void GuestEntry(uint32_t addr)
{
    if (addr == kThreadEntry)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (CallNid(kNidOutputBlocking,
                        static_cast<uint32_t>(g_live_ch),
                        0x8000u,
                        kGuestBuf0) >= 0)
            {
                g_live_calls_ok += 1;
            }
        }
        // buf == 0 drains queued audio on the channel until rest length reaches 0.
        if (CallNid(kNidOutputBlocking, static_cast<uint32_t>(g_live_ch), 0x8000u, 0) == 0)
        {
            g_live_rest_after = CallNid(kNidGetRestLength, static_cast<uint32_t>(g_live_ch));
        }
    }
}

} // namespace

int main()
{
    using namespace repsp::hle;
    using namespace repsp::hle::audio;

    auto& mem = repsp::runtime::GetGuestMemory();
    TEST_ASSERT(mem.Initialize());

    const repsp_guest_cpu_ops ops{
        sizeof(MockCpu), MockSave, MockRestore,
        MockGetGpr, MockSetGpr, GuestEntry,
        MockGetFpr, MockSetFpr,
    };
    repsp_recomp_register_cpu(&ops);
    repsp::runtime::GetScheduler().SetCpuOps(&ops);
    kernel::Register(kernel::KernelConfig{});

    AudioHleConfig cfg{};
    cfg.auto_attach_platform = false;
    Register(cfg);

    // 1. Reservation validation & channel allocation (auto and explicit).
    TEST_ASSERT(CallNid(kNidChReserve, 0u, 32u, kAudioFormatStereo) == kSceAudioErrorInvalidSize);
    TEST_ASSERT(CallNid(kNidChReserve, 0u, 65u, kAudioFormatStereo) == kSceAudioErrorInvalidSize);
    TEST_ASSERT(CallNid(kNidChReserve, 0u, 64u, 0x20u) == kSceAudioErrorInvalidFormat);
    TEST_ASSERT(CallNid(kNidChReserve, 8u, 64u, kAudioFormatStereo) == kSceAudioErrorInvalidCh);
    TEST_ASSERT(CallNid(kNidChReserve, static_cast<uint32_t>(-2), 64u, kAudioFormatStereo) ==
                kSceAudioErrorInvalidCh);

    const int32_t ch0 = CallNid(kNidChReserve, static_cast<uint32_t>(-1), 64u, kAudioFormatStereo);
    const int32_t ch1 = CallNid(kNidChReserve, 1u, 64u, kAudioFormatMono);
    TEST_ASSERT(ch0 == 0);
    TEST_ASSERT(ch1 == 1);
    TEST_ASSERT(CallNid(kNidChReserve, 1u, 64u, kAudioFormatStereo) == kSceAudioErrorOutputBusy);

    for (int i = 0; i < 6; ++i)
    {
        TEST_ASSERT(CallNid(kNidChReserve, static_cast<uint32_t>(-1), 64u, kAudioFormatStereo) >= 2);
    }
    TEST_ASSERT(CallNid(kNidChReserve, static_cast<uint32_t>(-1), 64u, kAudioFormatStereo) ==
                kSceAudioErrorNotFound);
    for (uint32_t c = 2; c < 8; ++c)
    {
        TEST_ASSERT(CallNid(kNidChRelease, c) == 0);
    }
    TEST_ASSERT(CallNid(kNidChRelease, 2u) == kSceAudioErrorNotReserved);

    // 2. SetChannelDataLen validation and update.
    TEST_ASSERT(CallNid(kNidSetDataLen, 2u, 128u) == kSceAudioErrorNotReserved);
    TEST_ASSERT(CallNid(kNidSetDataLen, 0u, 65u) == kSceAudioErrorInvalidSize);
    TEST_ASSERT(CallNid(kNidSetDataLen, 0u, 128u) == 0);
    TEST_ASSERT(CallNid(kNidSetDataLen, 0u, 64u) == 0);

    // 3. Stereo + mono panned mixing, clamping saturation, rest length, and underrun accounting.
    for (int i = 0; i < 64; ++i)
    {
        mem.WriteU16(kGuestBuf0 + static_cast<uint32_t>(i * 4), static_cast<uint16_t>(20000));
        mem.WriteU16(kGuestBuf0 + static_cast<uint32_t>(i * 4 + 2), static_cast<uint16_t>(-20000));
        mem.WriteU16(kGuestBuf1 + static_cast<uint32_t>(i * 2), static_cast<uint16_t>(20000));
    }

    TEST_ASSERT(CallNid(kNidOutputBlocking, 0u, 0x10000u, kGuestBuf0) == kSceAudioErrorInvalidVolume);
    TEST_ASSERT(CallNid(kNidOutputBlocking, 0u, static_cast<uint32_t>(-1), kGuestBuf0) == kSceAudioErrorInvalidVolume);
    TEST_ASSERT(CallNid(kNidOutputBlocking, 2u, 0x8000u, kGuestBuf0) == kSceAudioErrorNotReserved);
    TEST_ASSERT(CallNid(kNidOutputBlocking, 0u, 0x8000u, kGuestBuf0) == 64);
    TEST_ASSERT(CallNid(kNidOutputPannedBlocking, 1u, 0x8000u, 0x4000u, kGuestBuf1) == 64);

    TEST_ASSERT(CallNid(kNidGetRestLength, 0u) == 64);
    TEST_ASSERT(CallNid(kNidGetRestLength, 1u) == 64);
    TEST_ASSERT(CallNid(kNidGetRestLength, 2u) == kSceAudioErrorNotReserved);

    std::array<int16_t, 64> mixed{};
    MixPull(mixed.data(), 32);
    TEST_ASSERT(mixed[0] == 32767);
    TEST_ASSERT(mixed[1] == -10000);
    TEST_ASSERT(mixed[62] == 32767);
    TEST_ASSERT(mixed[63] == -10000);
    TEST_ASSERT(CallNid(kNidGetRestLength, 0u) == 32);

    MixPull(mixed.data(), 16);
    TEST_ASSERT(CallNid(kNidGetRestLength, 0u) == 16);

    // Partial queue drain mid-pull (16 frames remaining when pulling 32) triggers underrun + silence tail.
    mixed.fill(123);
    MixPull(mixed.data(), 32);
    TEST_ASSERT(CallNid(kNidGetRestLength, 0u) == 0);
    TEST_ASSERT(CallNid(kNidGetRestLength, 1u) == 0);
    TEST_ASSERT(mixed[32] == 0 && mixed[63] == 0);
    TEST_ASSERT(GetStats().underruns == 2);

    TEST_ASSERT(CallNid(kNidChRelease, 0u) == 0);
    TEST_ASSERT(CallNid(kNidChRelease, 1u) == 0);

    // 4. Real-time dummy audio backend pacing wakes blocking OutputBlocking & buf==0 drain.
    repsp::platform::PlatformConfig plat_cfg{};
    plat_cfg.headless = true;
    plat_cfg.display.dummy = true;
    plat_cfg.audio.dummy = true;
    TEST_ASSERT(repsp::platform::Initialize(plat_cfg));
    SetBackend(repsp::platform::GetAudio());

    g_live_ch = CallNid(kNidChReserve, static_cast<uint32_t>(-1), 64u, kAudioFormatStereo);
    TEST_ASSERT(g_live_ch >= 0);

    const int32_t tid =
        repsp::runtime::GetScheduler().CreateThread("audio_waiter", kThreadEntry, 0x20, 0x4000, 0, 0);
    TEST_ASSERT(tid > 0);
    repsp::runtime::GetScheduler().Run(tid);

    TEST_ASSERT(g_live_calls_ok == 3);
    TEST_ASSERT(g_live_rest_after == 0);
    const AudioStats stats = GetStats();
    TEST_ASSERT(stats.total_output_calls == 6);
    TEST_ASSERT(stats.total_frames_pulled > 0);
    const std::string formatted = FormatStats();
    TEST_ASSERT(formatted.find("reserved=") != std::string::npos);
    TEST_ASSERT(formatted.find("underruns=") != std::string::npos);

    Shutdown();
    repsp::platform::Shutdown();
    repsp::runtime::GetScheduler().Reset();
    mem.Shutdown();

    std::printf("[PASS] audio_probe: 4/4 checks passed\n");
    return 0;
}
