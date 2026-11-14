#include "repsp/hle/kernel.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/vblank_clock.h"

#include <atomic>
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
    uint32_t gpr[32] = {0};
};

MockCpu g_cpu;
void MockSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void MockRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t MockGetGpr(int idx) { return (idx > 0 && idx < 32) ? g_cpu.gpr[idx] : 0; }
void MockSetGpr(int idx, uint32_t val) { if (idx > 0 && idx < 32) g_cpu.gpr[idx] = val; }
uint32_t MockGetFpr(int) { return 0; }
void MockSetFpr(int, uint32_t) {}

std::atomic<uint32_t> g_sub_ticks{0};
std::vector<uint32_t> g_wake_observed_ticks;

void GuestThreadFunc(uint32_t addr)
{
    if (addr == 0x08801000u)
    {
        auto& clock = repsp::runtime::GetVblankClock();
        for (int i = 0; i < 3; ++i)
        {
            const int32_t rc = clock.WaitNext(false);
            if (rc < 0)
            {
                std::fprintf(stderr, "WaitNext failed: %d\n", rc);
                return;
            }
            g_wake_observed_ticks.push_back(g_sub_ticks.load());
        }
    }
}

} // namespace

int main()
{
    auto& clock = repsp::runtime::GetVblankClock();
    clock.ResetForTest();
    TEST_ASSERT(clock.Count() == 0);
    TEST_ASSERT(!clock.IsRunning());

    // 1. Start/Stop reference counting
    clock.Start();
    TEST_ASSERT(clock.IsRunning());
    clock.Start();
    TEST_ASSERT(clock.IsRunning());
    clock.Stop();
    TEST_ASSERT(clock.IsRunning());
    clock.Stop();
    TEST_ASSERT(!clock.IsRunning());

    // 2. Synchronous subscriber registration order & Unsubscribe via StepForTest
    std::vector<std::pair<char, uint32_t>> events;
    const uint32_t id_a = clock.Subscribe([&](uint32_t vcount) { events.push_back({'A', vcount}); });
    const uint32_t id_b = clock.Subscribe([&](uint32_t vcount) { events.push_back({'B', vcount}); });
    clock.StepForTest();
    clock.StepForTest();
    TEST_ASSERT(clock.Count() == 2);
    TEST_ASSERT(events.size() == 4);
    TEST_ASSERT(events[0].first == 'A' && events[0].second == 1);
    TEST_ASSERT(events[1].first == 'B' && events[1].second == 1);
    TEST_ASSERT(events[2].first == 'A' && events[2].second == 2);
    TEST_ASSERT(events[3].first == 'B' && events[3].second == 2);

    clock.Unsubscribe(id_a);
    clock.StepForTest();
    TEST_ASSERT(clock.Count() == 3);
    TEST_ASSERT(events.size() == 5);
    TEST_ASSERT(events[4].first == 'B' && events[4].second == 3);
    clock.Unsubscribe(id_b);

    // 3. Scheduler WaitNext + background timer thread: subscribers run before waiting guest wakes
    TEST_ASSERT(repsp::runtime::GetGuestMemory().Initialize());
    const repsp_guest_cpu_ops ops{
        sizeof(MockCpu), MockSave, MockRestore,
        MockGetGpr, MockSetGpr, GuestThreadFunc,
        MockGetFpr, MockSetFpr,
    };
    repsp_recomp_register_cpu(&ops);
    repsp::runtime::GetScheduler().SetCpuOps(&ops);
    repsp::hle::kernel::Register(repsp::hle::kernel::KernelConfig{});

    clock.ResetForTest();
    g_sub_ticks.store(0);
    g_wake_observed_ticks.clear();
    const uint32_t sub_id = clock.Subscribe([](uint32_t vcount)
    {
        g_sub_ticks.store(vcount);
    });
    clock.Start();

    const int32_t tid = repsp::runtime::GetScheduler().CreateThread("vblank_waiter", 0x08801000u, 0x20, 0x4000, 0, 0);
    TEST_ASSERT(tid > 0);
    repsp::runtime::GetScheduler().Run(tid);

    clock.Stop();
    clock.Unsubscribe(sub_id);

    TEST_ASSERT(g_wake_observed_ticks.size() == 3);
    TEST_ASSERT(g_wake_observed_ticks[0] >= 1);
    TEST_ASSERT(g_wake_observed_ticks[1] > g_wake_observed_ticks[0]);
    TEST_ASSERT(g_wake_observed_ticks[2] > g_wake_observed_ticks[1]);
    TEST_ASSERT(clock.Count() >= 3);

    repsp::runtime::GetScheduler().Reset();
    clock.ResetForTest();
    repsp::runtime::GetGuestMemory().Shutdown();

    std::printf("[PASS] vblank_probe: 3/3 checks passed\n");
    return 0;
}
