#include "repsp/hle/audio.h"
#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/platform/platform.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define TEST_ASSERT(cond)                                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(cond))                                                                   \
        {                                                                              \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);     \
            std::fflush(nullptr);                                                      \
            std::_Exit(1);                                                             \
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

    // 5 & 6. Intermediate buffer: large device pulls (1024 frames) with 256-sample channel.
    {
        class ProbeAudioBackend final : public repsp::platform::AudioBackend
        {
        public:
            bool Initialize(const repsp::platform::AudioConfig&) override { return true; }
            void Shutdown() override { cb_ = nullptr; }
            void SetPullCallback(repsp::platform::AudioPullCallback cb) override { cb_ = std::move(cb); }
            bool QueueAudio(const int16_t*, size_t) override { return true; }
            void Pull(size_t frames, std::vector<int16_t>& out)
            {
                out.assign(frames * 2u, 0);
                if (cb_) cb_(out.data(), frames);
            }
        private:
            repsp::platform::AudioPullCallback cb_;
        };

        ProbeAudioBackend mock_backend;
        AudioHleConfig lat_cfg{};
        lat_cfg.auto_attach_platform = false;
        lat_cfg.target_latency_ms = 30; // 1323 frames > 1024-frame pull
        Register(lat_cfg);
        SetBackend(&mock_backend);

        const int32_t ch = CallNid(kNidChReserve, 0u, 256u, kAudioFormatStereo);
        TEST_ASSERT(ch == 0);
        for (int i = 0; i < 256; ++i)
        {
            mem.WriteU16(kGuestBuf0 + static_cast<uint32_t>(i * 4), 1000);
            mem.WriteU16(kGuestBuf0 + static_cast<uint32_t>(i * 4 + 2), 1000);
        }

        // Write 6 blocks (1536 frames) so the 30 ms intermediate buffer fills above 1024 frames.
        for (int b = 0; b < 6; ++b)
        {
            TEST_ASSERT(CallNid(kNidOutputBlocking, 0u, 0x8000u, kGuestBuf0) == 256);
        }
        // Give the clock thread a moment to pull into the intermediate buffer.
        for (int w = 0; w < 50; ++w)
        {
            if (CallNid(kNidGetRestLength, 0u) <= 256)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::vector<int16_t> out;
        mock_backend.Pull(1024u, out);
        const AudioStats s5 = GetStats();
        TEST_ASSERT(s5.target_latency_ms == 30u);
        TEST_ASSERT(s5.target_buffer_frames == (30u * 44100u) / 1000u);
        TEST_ASSERT(s5.host_underruns == 0u);
        TEST_ASSERT(s5.buffer_depth_frames >= 1024u);
        TEST_ASSERT(out[0] == 1000 && out[2047] == 1000);

        // 6. Late producer: drain remaining frames without feeding new ones -> host_underruns > 0.
        mock_backend.Pull(1024u, out);
        mock_backend.Pull(1024u, out);
        const AudioStats s6 = GetStats();
        TEST_ASSERT(s6.host_underruns > 0u);
        TEST_ASSERT(s6.host_shortfall_frames > 0u);

        Shutdown();
        repsp::runtime::GetScheduler().Reset();
    }

    // 7. Clock thread holds off mixing silence for a temporarily late active channel
    //    while the intermediate buffer still has headroom.
    {
        class ProbeAudioBackend2 final : public repsp::platform::AudioBackend
        {
        public:
            bool Initialize(const repsp::platform::AudioConfig&) override { return true; }
            void Shutdown() override { cb_ = nullptr; }
            void SetPullCallback(repsp::platform::AudioPullCallback cb) override { cb_ = std::move(cb); }
            bool QueueAudio(const int16_t*, size_t) override { return true; }
            void Pull(size_t frames, std::vector<int16_t>& out)
            {
                out.assign(frames * 2u, 0);
                if (cb_) cb_(out.data(), frames);
            }
        private:
            repsp::platform::AudioPullCallback cb_;
        };

        ProbeAudioBackend2 mock_backend;
        AudioHleConfig lat_cfg{};
        lat_cfg.auto_attach_platform = false;
        lat_cfg.target_latency_ms = 20; // 882 frames
        lat_cfg.auto_start_clock_for_test = false;
        Register(lat_cfg);
        SetBackend(&mock_backend);

        TEST_ASSERT(CallNid(kNidChReserve, 0u, 256u, kAudioFormatStereo) == 0);
        TEST_ASSERT(CallNid(kNidChReserve, 1u, 256u, kAudioFormatStereo) == 1);
        for (int i = 0; i < 256; ++i)
        {
            mem.WriteU16(kGuestBuf0 + static_cast<uint32_t>(i * 4), 1000);
            mem.WriteU16(kGuestBuf0 + static_cast<uint32_t>(i * 4 + 2), 1000);
            mem.WriteU16(kGuestBuf1 + static_cast<uint32_t>(i * 4), 500);
            mem.WriteU16(kGuestBuf1 + static_cast<uint32_t>(i * 4 + 2), 500);
        }

        for (int b = 0; b < 4; ++b)
        {
            TEST_ASSERT(CallNid(kNidOutputBlocking, 0u, 0x8000u, kGuestBuf0) == 256);
            TEST_ASSERT(CallNid(kNidOutputBlocking, 1u, 0x8000u, kGuestBuf1) == 256);
            while (StepMixClockForTest()) {}
        }

        // Pull 256 frames: recent_pull becomes 256, low_headroom becomes 384,
        // and intermediate buffer depth drops from 896 to 640 (> 384).
        std::vector<int16_t> out;
        mock_backend.Pull(256u, out);
        TEST_ASSERT(out[0] == 1500 && out[511] == 1500);

        // Drain the remaining 128 frames on ch0/ch1 (depth -> 768) and reset stats.
        while (StepMixClockForTest()) {}
        TEST_ASSERT(CallNid(kNidGetRestLength, 0u) == 0);
        TEST_ASSERT(CallNid(kNidGetRestLength, 1u) == 0);
        ResetStatsForTest();

        // Feed ch0 only; with depth 768 > low_headroom 384, the clock must hold off.
        TEST_ASSERT(CallNid(kNidOutputBlocking, 0u, 0x8000u, kGuestBuf0) == 256);
        TEST_ASSERT(!StepMixClockForTest());

        // Feed ch1; now both active channels are ready and mix up to target_buffer_frames (896 >= 882).
        TEST_ASSERT(CallNid(kNidOutputBlocking, 1u, 0x8000u, kGuestBuf1) == 256);
        TEST_ASSERT(StepMixClockForTest());
        TEST_ASSERT(CallNid(kNidGetRestLength, 1u) == 128);
        mock_backend.Pull(256u, out);
        while (StepMixClockForTest()) {}
        TEST_ASSERT(CallNid(kNidGetRestLength, 1u) == 0);

        const AudioStats s7 = GetStats();
        TEST_ASSERT(s7.channels[0].underruns == 0u);
        TEST_ASSERT(s7.channels[1].underruns == 0u);
        TEST_ASSERT(s7.underruns == 0u);

        Shutdown();
        repsp::runtime::GetScheduler().Reset();
    }

    // 8. Device pull size raises the low-headroom floor (recent_pull + quantum) so
    //    host_underruns stays 0 even when an active channel is late below that floor.
    {
        class ProbeAudioBackend3 final : public repsp::platform::AudioBackend
        {
        public:
            bool Initialize(const repsp::platform::AudioConfig&) override { return true; }
            void Shutdown() override { cb_ = nullptr; }
            void SetPullCallback(repsp::platform::AudioPullCallback cb) override { cb_ = std::move(cb); }
            bool QueueAudio(const int16_t*, size_t) override { return true; }
            void Pull(size_t frames, std::vector<int16_t>& out)
            {
                out.assign(frames * 2u, 0);
                if (cb_) cb_(out.data(), frames);
            }
        private:
            repsp::platform::AudioPullCallback cb_;
        };

        ProbeAudioBackend3 mock_backend;
        AudioHleConfig lat_cfg{};
        lat_cfg.auto_attach_platform = false;
        lat_cfg.target_latency_ms = 20; // 882 frames
        lat_cfg.auto_start_clock_for_test = false;
        Register(lat_cfg);
        SetBackend(&mock_backend);

        TEST_ASSERT(CallNid(kNidChReserve, 0u, 256u, kAudioFormatStereo) == 0);
        TEST_ASSERT(CallNid(kNidChReserve, 1u, 256u, kAudioFormatStereo) == 1);
        for (int i = 0; i < 256; ++i)
        {
            mem.WriteU16(kGuestBuf0 + static_cast<uint32_t>(i * 4), 1000);
            mem.WriteU16(kGuestBuf0 + static_cast<uint32_t>(i * 4 + 2), 1000);
            mem.WriteU16(kGuestBuf1 + static_cast<uint32_t>(i * 4), 500);
            mem.WriteU16(kGuestBuf1 + static_cast<uint32_t>(i * 4 + 2), 500);
        }

        for (int b = 0; b < 4; ++b)
        {
            TEST_ASSERT(CallNid(kNidOutputBlocking, 0u, 0x8000u, kGuestBuf0) == 256);
            TEST_ASSERT(CallNid(kNidOutputBlocking, 1u, 0x8000u, kGuestBuf1) == 256);
            while (StepMixClockForTest()) {}
        }

        // Pull 512 frames: recent_pull becomes 512, low_headroom becomes 512 + 128 = 640,
        // and intermediate buffer depth drops from 896 to 384 (<= 640).
        std::vector<int16_t> out;
        mock_backend.Pull(512u, out);
        TEST_ASSERT(out[1022] == 1500 && out[1023] == 1500);

        // Drain the remaining 128 frames on ch0/ch1 (depth -> 512 <= 640) and reset stats.
        while (StepMixClockForTest()) {}
        TEST_ASSERT(CallNid(kNidGetRestLength, 0u) == 0);
        TEST_ASSERT(CallNid(kNidGetRestLength, 1u) == 0);
        ResetStatsForTest();

        // Feed only ch0 (leaving ch1 empty). Because depth (512) <= low_headroom (640),
        // the clock steps immediately rather than waiting, so a second 512-frame pull
        // succeeds with 0 host underruns while ch1 records the forced channel underrun.
        TEST_ASSERT(CallNid(kNidOutputBlocking, 0u, 0x8000u, kGuestBuf0) == 256);
        TEST_ASSERT(StepMixClockForTest());
        TEST_ASSERT(StepMixClockForTest());
        TEST_ASSERT(CallNid(kNidGetRestLength, 0u) == 0);
        mock_backend.Pull(512u, out);
        const AudioStats s8 = GetStats();
        TEST_ASSERT(s8.host_underruns == 0u);
        TEST_ASSERT(s8.channels[1].underruns > 0u);

        Shutdown();
        repsp::runtime::GetScheduler().Reset();
    }

    mem.Shutdown();

    std::printf("[PASS] audio_probe: 8/8 checks passed\n");
    return 0;
}
