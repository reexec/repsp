#include "repsp/hle/ctrl.h"
#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/platform/input.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/vblank_clock.h"

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

constexpr uint32_t kNidSetSamplingCycle = 0x6A2774F3u;
constexpr uint32_t kNidSetSamplingMode = 0x1F4011E6u;
constexpr uint32_t kNidReadBufferPositive = 0x1F803938u;

struct MockCpu
{
    uint32_t gpr[32] = {0};
};

MockCpu g_cpu;

void MockSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void MockRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t MockGetGpr(int idx) { return (idx > 0 && idx < 32) ? g_cpu.gpr[idx] : 0; }
void MockSetGpr(int idx, uint32_t val) { if (idx > 0 && idx < 32) g_cpu.gpr[idx] = val; }
uint32_t MockGetFpr(int) { return 0; }
void MockSetFpr(int, uint32_t) {}

int32_t CallCycle(int32_t cycle)
{
    MockSetGpr(4, static_cast<uint32_t>(cycle));
    repsp_recomp_hle_call(kNidSetSamplingCycle);
    return static_cast<int32_t>(MockGetGpr(2));
}

int32_t CallMode(int32_t mode)
{
    MockSetGpr(4, static_cast<uint32_t>(mode));
    repsp_recomp_hle_call(kNidSetSamplingMode);
    return static_cast<int32_t>(MockGetGpr(2));
}

int32_t CallReadPositive(uint32_t pad_addr, int32_t count)
{
    MockSetGpr(4, pad_addr);
    MockSetGpr(5, static_cast<uint32_t>(count));
    repsp_recomp_hle_call(kNidReadBufferPositive);
    return static_cast<int32_t>(MockGetGpr(2));
}

repsp::hle::ctrl::SceCtrlData ReadPadFromGuest(uint32_t addr)
{
    repsp::hle::ctrl::SceCtrlData d{};
    repsp::runtime::GetGuestMemory().ReadBlock(addr, &d, sizeof(d));
    return d;
}

std::vector<repsp::hle::ctrl::SceCtrlData> g_sched_reads;

void GuestEntry(uint32_t addr)
{
    if (addr == 0x08802000u)
    {
        constexpr uint32_t kGuestPad = 0x08920000u;
        for (int i = 0; i < 2; ++i)
        {
            const int32_t n = CallReadPositive(kGuestPad, 1);
            if (n != 1)
            {
                std::fprintf(stderr, "Scheduled ReadBufferPositive returned %d\n", n);
                return;
            }
            g_sched_reads.push_back(ReadPadFromGuest(kGuestPad));
        }
    }
}

} // namespace

int main()
{
    auto& mem = repsp::runtime::GetGuestMemory();
    TEST_ASSERT(mem.Initialize());

    const repsp_guest_cpu_ops ops{
        sizeof(MockCpu), MockSave, MockRestore,
        MockGetGpr, MockSetGpr, GuestEntry,
        MockGetFpr, MockSetFpr,
    };
    repsp_recomp_register_cpu(&ops);
    repsp::runtime::GetScheduler().SetCpuOps(&ops);
    repsp::hle::kernel::Register(repsp::hle::kernel::KernelConfig{});

    auto& clock = repsp::runtime::GetVblankClock();
    clock.ResetForTest();

    repsp::hle::ctrl::Register();
    // Pause background timer for deterministic StepForTest checks (balanced before Shutdown)
    clock.Stop();

    // 1. Sampling cycle valid and invalid values
    TEST_ASSERT(CallCycle(-1) == repsp::hle::kSceKernelErrorInvalidValue);
    TEST_ASSERT(CallCycle(100) == 0);
    TEST_ASSERT(CallCycle(10000) == 100);
    TEST_ASSERT(CallCycle(0) == 10000);

    // 2. Sampling mode valid and invalid values
    TEST_ASSERT(CallMode(-1) == repsp::hle::kSceKernelErrorInvalidMode);
    TEST_ASSERT(CallMode(2) == repsp::hle::kSceKernelErrorInvalidMode);
    TEST_ASSERT(CallMode(repsp::hle::ctrl::kPspCtrlModeAnalog) == repsp::hle::ctrl::kPspCtrlModeDigital);
    TEST_ASSERT(CallMode(repsp::hle::ctrl::kPspCtrlModeDigital) == repsp::hle::ctrl::kPspCtrlModeAnalog);

    // 3. ReadBufferPositive error validation
    constexpr uint32_t kPadBuf = 0x08910000u;
    TEST_ASSERT(CallReadPositive(kPadBuf, 0) == repsp::hle::kSceKernelErrorInvalidSize);
    TEST_ASSERT(CallReadPositive(kPadBuf, -1) == repsp::hle::kSceKernelErrorInvalidSize);
    TEST_ASSERT(CallReadPositive(kPadBuf, 65) == repsp::hle::kSceKernelErrorInvalidSize);
    TEST_ASSERT(CallReadPositive(0, 1) == repsp::hle::kSceKernelErrorInvalidPointer);
    TEST_ASSERT(CallReadPositive(0x12340000u, 1) == repsp::hle::kSceKernelErrorInvalidPointer);
    TEST_ASSERT(CallReadPositive(0x09FFFFF8u, 1) == repsp::hle::kSceKernelErrorInvalidPointer);

    // 4. Input injection + Digital vs Analog axis clamping
    float inj_lx = -1.0f;
    float inj_ly = 0.5f;
    uint32_t inj_buttons = repsp::platform::kPspCtrlCross | repsp::platform::kPspCtrlStart;
    repsp::hle::ctrl::SetInputSource([&](uint32_t /*vcount*/)
    {
        repsp::platform::ControllerState s{};
        s.buttons = inj_buttons;
        s.lx = inj_lx;
        s.ly = inj_ly;
        return s;
    });

    TEST_ASSERT(CallMode(repsp::hle::ctrl::kPspCtrlModeDigital) >= 0);
    clock.StepForTest();
    TEST_ASSERT(CallReadPositive(kPadBuf, 1) == 1);
    {
        const auto d = ReadPadFromGuest(kPadBuf);
        TEST_ASSERT(d.buttons == inj_buttons);
        TEST_ASSERT(d.lx == 128);
        TEST_ASSERT(d.ly == 128);
        TEST_ASSERT(d.time_stamp > 0);
    }

    TEST_ASSERT(CallMode(repsp::hle::ctrl::kPspCtrlModeAnalog) == repsp::hle::ctrl::kPspCtrlModeDigital);
    clock.StepForTest();
    TEST_ASSERT(CallReadPositive(kPadBuf, 1) == 1);
    {
        const auto d = ReadPadFromGuest(kPadBuf);
        TEST_ASSERT(d.buttons == inj_buttons);
        TEST_ASSERT(d.lx == 0);
        TEST_ASSERT(d.ly == 191);
    }

    inj_lx = 0.0f;
    inj_ly = 1.0f;
    clock.StepForTest();
    TEST_ASSERT(CallReadPositive(kPadBuf, 1) == 1);
    {
        const auto d = ReadPadFromGuest(kPadBuf);
        TEST_ASSERT(d.lx == 128);
        TEST_ASSERT(d.ly == 255);
    }

    // 5. 64-entry ring buffer chronological order and wraparound (>64 samples)
    repsp::hle::ctrl::SetInputSource([](uint32_t vcount)
    {
        repsp::platform::ControllerState s{};
        s.buttons = vcount;
        return s;
    });
    for (int i = 0; i < 4; ++i)
    {
        clock.StepForTest();
    }
    TEST_ASSERT(CallReadPositive(kPadBuf, 4) == 4);
    for (int i = 0; i < 3; ++i)
    {
        const auto a = ReadPadFromGuest(kPadBuf + static_cast<uint32_t>(i * 16));
        const auto b = ReadPadFromGuest(kPadBuf + static_cast<uint32_t>((i + 1) * 16));
        TEST_ASSERT(b.time_stamp > a.time_stamp);
        TEST_ASSERT(b.buttons == a.buttons + 1u);
    }

    for (int i = 0; i < 70; ++i)
    {
        clock.StepForTest();
    }
    TEST_ASSERT(CallReadPositive(kPadBuf, 64) == 64);
    for (int i = 0; i < 63; ++i)
    {
        const auto a = ReadPadFromGuest(kPadBuf + static_cast<uint32_t>(i * 16));
        const auto b = ReadPadFromGuest(kPadBuf + static_cast<uint32_t>((i + 1) * 16));
        TEST_ASSERT(b.time_stamp > a.time_stamp);
        TEST_ASSERT(b.buttons == a.buttons + 1u);
    }
    const auto newest = ReadPadFromGuest(kPadBuf + 63u * 16u);
    TEST_ASSERT(newest.buttons == clock.Count());

    // 6. Scheduler blocking wait across live VBlank ticks + stats verification
    g_sched_reads.clear();
    clock.Start();
    const int32_t tid = repsp::runtime::GetScheduler().CreateThread("ctrl_waiter", 0x08802000u, 0x20, 0x4000, 0, 0);
    TEST_ASSERT(tid > 0);
    repsp::runtime::GetScheduler().Run(tid);

    TEST_ASSERT(g_sched_reads.size() == 2);
    TEST_ASSERT(g_sched_reads[1].time_stamp > g_sched_reads[0].time_stamp);
    const auto stats = repsp::hle::ctrl::GetStats();
    TEST_ASSERT(stats.samples_produced >= 79);
    TEST_ASSERT(stats.samples_read == 73);
    TEST_ASSERT(repsp::hle::ctrl::FormatStats().find("Ctrl:") != std::string::npos);

    repsp::hle::ctrl::Shutdown();
    repsp::runtime::GetScheduler().Reset();
    clock.ResetForTest();
    mem.Shutdown();

    std::printf("[PASS] ctrl_probe: 6/6 checks passed\n");
    return 0;
}
