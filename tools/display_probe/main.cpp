#include "repsp/hle/display.h"
#include "repsp/hle/kernel.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
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
    uint32_t gpr[32] = {0};
    uint32_t fpr[32] = {0};
};

MockCpuState g_cpu;
repsp::runtime::NidRegistry* g_nids = nullptr;

constexpr uint32_t kNidDisplaySetMode = 0x0E20F177u;
constexpr uint32_t kNidDisplaySetFrameBuf = 0x289D82FEu;
constexpr uint32_t kNidDisplayWaitVblankStart = 0x984C27E7u;
constexpr uint32_t kNidDisplayGetVcount = 0x9C6EAAD7u;
constexpr uint32_t kNidDisplayGetFramePerSec = 0xDBA6C4C4u;

constexpr uint32_t kVblankThreadEntry = 0x08902000u;
uint32_t g_vblank_thread_vcount_before = 0;
uint32_t g_vblank_thread_vcount_after = 0;
bool g_vblank_thread_finished = false;

void MockSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void MockRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t MockGetGpr(int reg) { return (reg >= 0 && reg < 32) ? g_cpu.gpr[reg] : 0; }
void MockSetGpr(int reg, uint32_t val) { if (reg > 0 && reg < 32) g_cpu.gpr[reg] = val; }
uint32_t MockGetFpr(int reg) { return (reg >= 0 && reg < 32) ? g_cpu.fpr[reg] : 0; }
void MockSetFpr(int reg, uint32_t val) { if (reg >= 0 && reg < 32) g_cpu.fpr[reg] = val; }
void MockCallGuest(uint32_t addr)
{
    if (addr == kVblankThreadEntry)
    {
        TEST_ASSERT(g_nids->Invoke(kNidDisplayGetVcount));
        g_vblank_thread_vcount_before = g_cpu.gpr[2];
        TEST_ASSERT(g_nids->Invoke(kNidDisplayWaitVblankStart));
        TEST_ASSERT(g_nids->Invoke(kNidDisplayGetVcount));
        g_vblank_thread_vcount_after = g_cpu.gpr[2];
        g_vblank_thread_finished = true;
    }
}

repsp_guest_cpu_ops MakeMockOps()
{
    repsp_guest_cpu_ops ops{};
    ops.context_size = sizeof(MockCpuState);
    ops.save = MockSave;
    ops.restore = MockRestore;
    ops.get_gpr = MockGetGpr;
    ops.set_gpr = MockSetGpr;
    ops.call_guest = MockCallGuest;
    ops.get_fpr = MockGetFpr;
    ops.set_fpr = MockSetFpr;
    return ops;
}

uint32_t InvokeNid(uint32_t nid, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0)
{
    g_cpu.gpr[4] = a0;
    g_cpu.gpr[5] = a1;
    g_cpu.gpr[6] = a2;
    g_cpu.gpr[7] = a3;
    TEST_ASSERT(g_nids->Invoke(nid));
    return g_cpu.gpr[2];
}

} // namespace

int main()
{
    auto& mem = repsp::runtime::GetGuestMemory();
    TEST_ASSERT(mem.Initialize());

    auto& nids = repsp::runtime::GetNidRegistry();
    auto& sched = repsp::runtime::GetScheduler();
    g_nids = &nids;
    repsp_guest_cpu_ops ops = MakeMockOps();
    repsp_recomp_register_cpu(&ops);
    repsp::hle::kernel::Register(repsp::hle::kernel::KernelConfig{});

    repsp::hle::display::DisplayHleConfig cfg;
    cfg.headless = true;
    cfg.window_title = "display_probe";
    repsp::hle::display::Register(cfg);

    // 1. ConvertFrameBufferToRgba8 across all 4 PSP pixel formats (565, 5551, 4444, 8888)
    {
        constexpr uint32_t kFb = 0x04000000u;
        std::vector<uint8_t> rgba(480 * 272 * 4, 0);

        // Format 0: RGB565 (R=31, G=0, B=0 -> 0x001F; R=0, G=63, B=0 -> 0x07E0)
        repsp_guest_write16(kFb + 0, 0x001Fu);
        repsp_guest_write16(kFb + 2, 0x07E0u);
        repsp::hle::display::ConvertFrameBufferToRgba8(kFb, 512, 0, rgba.data());
        TEST_ASSERT(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255);
        TEST_ASSERT(rgba[4] == 0 && rgba[5] == 255 && rgba[6] == 0 && rgba[7] == 255);

        // Format 1: ABGR1555 (R=31 -> 0x001F; B=31 -> 0x7C00)
        repsp_guest_write16(kFb + 0, 0x801Fu);
        repsp_guest_write16(kFb + 2, 0xFC00u);
        repsp::hle::display::ConvertFrameBufferToRgba8(kFb, 512, 1, rgba.data());
        TEST_ASSERT(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 255);
        TEST_ASSERT(rgba[4] == 0 && rgba[5] == 0 && rgba[6] == 255 && rgba[7] == 255);

        // Format 2: ABGR4444 (0xF248 -> A=FF, B=22, G=44, R=88)
        repsp_guest_write16(kFb + 0, 0xF248u);
        repsp::hle::display::ConvertFrameBufferToRgba8(kFb, 512, 2, rgba.data());
        TEST_ASSERT(rgba[0] == 0x88 && rgba[1] == 0x44 && rgba[2] == 0x22 && rgba[3] == 0xFF);

        // Format 3: ABGR8888 (0xAA332211 -> R=11, G=22, B=33, A=FF forced by display)
        repsp_guest_write32(kFb + 0, 0xAA332211u);
        repsp::hle::display::ConvertFrameBufferToRgba8(kFb, 512, 3, rgba.data());
        TEST_ASSERT(rgba[0] == 0x11 && rgba[1] == 0x22 && rgba[2] == 0x33 && rgba[3] == 0xFF);
    }

    // 2. sceDisplaySetMode + sceDisplaySetFrameBuf (sync 0 immediate vs sync 1 nextframe)
    TEST_ASSERT(InvokeNid(kNidDisplaySetMode, 0, 480, 272) == 0);
    TEST_ASSERT(InvokeNid(kNidDisplaySetFrameBuf, 0x04000000u, 512, 3) == 0);
    TEST_ASSERT(repsp::hle::display::GetStats().active_topaddr == 0x04000000u);

    // Pass sync=1 in a3: remains pending until next vblank
    g_cpu.gpr[4] = 0x04088000u;
    g_cpu.gpr[5] = 512u;
    g_cpu.gpr[6] = 3u;
    g_cpu.gpr[7] = 1u;
    TEST_ASSERT(nids.Invoke(kNidDisplaySetFrameBuf));
    TEST_ASSERT(g_cpu.gpr[2] == 0);
    TEST_ASSERT(repsp::hle::display::GetStats().active_topaddr == 0x04000000u);

    // Wait >1 vblank (16.7ms) for pending framebuffer promotion
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    const auto stats = repsp::hle::display::GetStats();
    TEST_ASSERT(stats.active_topaddr == 0x04088000u);
    TEST_ASSERT(stats.active_bufferwidth == 512u);
    TEST_ASSERT(stats.active_pixelformat == 3);
    TEST_ASSERT(stats.frames_presented == 2);

    // Wait more vblanks without calling SetFrameBuf: vblank_count advances, frames_presented stays 2
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    const auto stats_idle = repsp::hle::display::GetStats();
    TEST_ASSERT(stats_idle.vblank_count > stats.vblank_count);
    TEST_ASSERT(stats_idle.frames_presented == 2);

    // topaddr == 0 presents a black frame when latched
    TEST_ASSERT(InvokeNid(kNidDisplaySetFrameBuf, 0u, 512u, 3u) == 0);
    const auto stats_black = repsp::hle::display::GetStats();
    TEST_ASSERT(stats_black.active_topaddr == 0u);
    TEST_ASSERT(stats_black.frames_presented == 3);

    // 3. sceDisplayGetFramePerSec -> 59.94005966f in $f0
    InvokeNid(kNidDisplayGetFramePerSec);
    float fps = 0.0f;
    std::memcpy(&fps, &g_cpu.fpr[0], sizeof(fps));
    TEST_ASSERT(std::fabs(fps - 59.94005966f) < 1e-4f);

    // 4. sceDisplayGetVcount advances with host elapsed time (59.94 Hz)
    const uint32_t vc0 = InvokeNid(kNidDisplayGetVcount);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const uint32_t vc1 = InvokeNid(kNidDisplayGetVcount);
    // 40ms * 59.94Hz ~= 2.4 vblanks; allow [1, 20] for scheduler jitter
    TEST_ASSERT(vc1 >= vc0 + 1 && vc1 <= vc0 + 20);

    // 5. sceDisplayWaitVblankStart blocks and wakes via Scheduler
    sched.SetCpuOps(&ops);
    const auto uid = sched.CreateThread("vblank_waiter", kVblankThreadEntry, 32, 0x4000, 0, 0);
    TEST_ASSERT(uid > 0);
    TEST_ASSERT(sched.StartThread(uid, 0, 0) == 0);
    sched.Run(uid);
    TEST_ASSERT(g_vblank_thread_finished);
    TEST_ASSERT(g_vblank_thread_vcount_after > g_vblank_thread_vcount_before);

    repsp::hle::display::Shutdown();
    std::printf("display_probe passed.\n");
    return 0;
}
