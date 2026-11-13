#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/sha1.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

struct FakeCpuState
{
    uint32_t gpr[32] = {};
};

FakeCpuState g_cpu;

void FakeSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void FakeRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t FakeGetGpr(int idx) { return (idx >= 0 && idx < 32) ? g_cpu.gpr[idx] : 0; }
void FakeSetGpr(int idx, uint32_t v) { if (idx > 0 && idx < 32) g_cpu.gpr[idx] = v; }

uint32_t HleCall(const char* name, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t t0 = 0)
{
    g_cpu.gpr[4] = a0;
    g_cpu.gpr[5] = a1;
    g_cpu.gpr[6] = a2;
    g_cpu.gpr[7] = a3;
    g_cpu.gpr[8] = t0;
    repsp_recomp_hle_call(repsp::runtime::ComputeNid(name));
    return g_cpu.gpr[2];
}

void WriteStr(uint32_t addr, const char* s)
{
    std::memcpy(repsp::runtime::GetGuestMemory().Translate(addr), s, std::strlen(s) + 1);
}

// Shared test trace & synchronization handles
std::vector<std::string> g_log;
int32_t g_sema = 0;
int32_t g_evf = 0;
int32_t g_cb = 0;
int g_cb_calls = 0;

constexpr uint32_t kAddrPrioMain = 0x08801000u;
constexpr uint32_t kAddrPrioHigh = 0x08801010u;
constexpr uint32_t kAddrFifoA    = 0x08802000u;
constexpr uint32_t kAddrFifoB    = 0x08802010u;
constexpr uint32_t kAddrSemaWait = 0x08803000u;
constexpr uint32_t kAddrEvfWait  = 0x08804000u;
constexpr uint32_t kAddrSleep    = 0x08805000u;
constexpr uint32_t kAddrCbMain   = 0x08806000u;
constexpr uint32_t kAddrCbFunc   = 0x08806010u;
constexpr uint32_t kAddrDeadlock = 0x08807000u;

void FakeCallGuest(uint32_t addr)
{
    using namespace repsp::runtime;
    using namespace repsp::hle;

    switch (addr)
    {
    case kAddrPrioMain:
    {
        g_log.push_back("main:start");
        WriteStr(0x09000100u, "high");
        int32_t hi = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000100u, kAddrPrioHigh, 0x10, 0x4000, 0));
        TEST_ASSERT(hi > 0);
        // Starting higher-priority thread (0x10 < 0x20) must preempt immediately!
        HleCall("sceKernelStartThread", static_cast<uint32_t>(hi), 0, 0);
        g_log.push_back("main:after_start");
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(hi), 0);
        g_log.push_back("main:done");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrPrioHigh:
    {
        g_log.push_back("high:run");
        HleCall("sceKernelDelayThread", 1000);
        g_log.push_back("high:exit");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrFifoA:
    {
        g_log.push_back("fifoA:1");
        WriteStr(0x09000200u, "fifoB");
        int32_t b = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000200u, kAddrFifoB, 0x20, 0x4000, 0));
        HleCall("sceKernelStartThread", static_cast<uint32_t>(b), 0, 0);
        HleCall("sceKernelDelayThread", 0); // yield at same priority
        g_log.push_back("fifoA:2");
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(b), 0);
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrFifoB:
    {
        g_log.push_back("fifoB:1");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrSemaWait:
    {
        // Timeout test first
        GetGuestMemory().WriteU32(0x09000300u, 1000u);
        int32_t to = static_cast<int32_t>(HleCall("sceKernelWaitSema", static_cast<uint32_t>(g_sema), 1, 0x09000300u));
        TEST_ASSERT(to == kSceKernelErrorWaitTimeout);
        g_log.push_back("sema:timeout_ok");

        // Start a waker thread that signals sema and evf
        WriteStr(0x09000310u, "waker");
        int32_t w = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000310u, kAddrEvfWait, 0x20, 0x4000, 0));
        HleCall("sceKernelStartThread", static_cast<uint32_t>(w), 0, 0);

        int32_t ok = static_cast<int32_t>(HleCall("sceKernelWaitSema", static_cast<uint32_t>(g_sema), 1, 0));
        TEST_ASSERT(ok == 0);
        g_log.push_back("sema:acquired");

        // Wait event flag AND + CLEAR
        int32_t ev = static_cast<int32_t>(HleCall("sceKernelWaitEventFlag", static_cast<uint32_t>(g_evf), 0x03, 0x20 /*CLEAR*/, 0x09000320u, 0));
        TEST_ASSERT(ev == 0);
        TEST_ASSERT(GetGuestMemory().ReadU32(0x09000320u) == 0x03u);
        // Bits must be cleared now
        int32_t poll_miss = static_cast<int32_t>(HleCall("sceKernelPollEventFlag", static_cast<uint32_t>(g_evf), 0x03, 0x01 /*OR*/, 0));
        TEST_ASSERT(poll_miss == kSceKernelErrorEvfCond);
        g_log.push_back("evf:ok");

        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(w), 0);
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrEvfWait:
    {
        HleCall("sceKernelSignalSema", static_cast<uint32_t>(g_sema), 1);
        HleCall("sceKernelSetEventFlag", static_cast<uint32_t>(g_evf), 0x01);
        HleCall("sceKernelDelayThread", 500);
        HleCall("sceKernelSetEventFlag", static_cast<uint32_t>(g_evf), 0x02);
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrSleep:
    {
        int32_t self = static_cast<int32_t>(HleCall("sceKernelGetThreadId"));
        HleCall("sceKernelWakeupThread", static_cast<uint32_t>(self)); // pre-wakeup
        TEST_ASSERT(HleCall("sceKernelSleepThread") == 0);
        g_log.push_back("sleep:ok");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrCbMain:
    {
        WriteStr(0x09000400u, "test_cb");
        g_cb = static_cast<int32_t>(HleCall("sceKernelCreateCallback", 0x09000400u, kAddrCbFunc, 0x1234u));
        TEST_ASSERT(g_cb > 0);
        TEST_ASSERT(GetScheduler().NotifyCallback(g_cb, 7) == 0);
        // DelayThreadCB runs pending callback; returning 1 deletes callback
        HleCall("sceKernelDelayThreadCB", 100);
        TEST_ASSERT(g_cb_calls == 1);
        // Notifying deleted callback returns unknown CBID
        TEST_ASSERT(GetScheduler().NotifyCallback(g_cb, 8) == kSceKernelErrorUnknownCbid);
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrCbFunc:
    {
        TEST_ASSERT(g_cpu.gpr[4] == 1);      // notify_count
        TEST_ASSERT(g_cpu.gpr[5] == 7);      // arg
        TEST_ASSERT(g_cpu.gpr[6] == 0x1234u); // common_arg
        ++g_cb_calls;
        g_cpu.gpr[2] = 1; // non-zero deletes callback
        break;
    }
    case kAddrDeadlock:
    {
        HleCall("sceKernelSleepThread"); // infinite sleep with no wakers -> deadlock
        g_cpu.gpr[2] = 0;
        break;
    }
    default:
        TEST_ASSERT(false);
    }
}

const repsp_guest_cpu_ops g_fake_ops = {
    sizeof(FakeCpuState), FakeSave, FakeRestore, FakeGetGpr, FakeSetGpr, FakeCallGuest
};

} // namespace

int main()
{
    using namespace repsp::runtime;
    using namespace repsp::hle;

    TEST_ASSERT(GetGuestMemory().Initialize());
    repsp_recomp_register_cpu(&g_fake_ops);

    // 1. Priority preemption & WaitThreadEnd
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    int32_t t1 = GetScheduler().CreateThread("main", kAddrPrioMain, 0x20, 0x4000, 0, 0);
    TEST_ASSERT(t1 > 0);
    GetScheduler().Run(t1);
    if (g_log != std::vector<std::string>{ "main:start", "high:run", "main:after_start", "high:exit", "main:done" })
    {
        for (const auto& s : g_log)
        {
            std::fprintf(stderr, "  g_log: %s\n", s.c_str());
        }
    }
    TEST_ASSERT((g_log == std::vector<std::string>{ "main:start", "high:run", "main:after_start", "high:exit", "main:done" }));

    // 2. Same-priority FIFO
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    int32_t ta = GetScheduler().CreateThread("fifoA", kAddrFifoA, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(ta);
    TEST_ASSERT((g_log == std::vector<std::string>{ "fifoA:1", "fifoB:1", "fifoA:2" }));

    // 3. Sema & EventFlag (timeout, signal, AND/CLEAR, poll)
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    WriteStr(0x09000500u, "sema0");
    WriteStr(0x09000520u, "evf0");
    g_sema = static_cast<int32_t>(HleCall("sceKernelCreateSema", 0x09000500u, 0, 0, 1));
    g_evf = static_cast<int32_t>(HleCall("sceKernelCreateEventFlag", 0x09000520u, 0, 0));
    TEST_ASSERT(g_sema > 0 && g_evf > 0);
    int32_t ts = GetScheduler().CreateThread("sema_evf", kAddrSemaWait, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(ts);
    TEST_ASSERT((g_log == std::vector<std::string>{ "sema:timeout_ok", "sema:acquired", "evf:ok" }));

    // 4. Sleep / Wakeup
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    int32_t tw = GetScheduler().CreateThread("sleep", kAddrSleep, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(tw);
    TEST_ASSERT((g_log == std::vector<std::string>{ "sleep:ok" }));

    // 5. Callbacks
    kernel::Register(kernel::KernelConfig{});
    g_cb_calls = 0;
    int32_t tc = GetScheduler().CreateThread("cb", kAddrCbMain, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(tc);
    TEST_ASSERT(g_cb_calls == 1);

    // 6. Deadlock stop reporting via GuardedRun
    kernel::Register(kernel::KernelConfig{});
    int32_t td = GetScheduler().CreateThread("deadlock_t", kAddrDeadlock, 0x20, 0x4000, 0, 0);
    auto guard = repsp::platform::GuardedRun([&]()
    {
        GetScheduler().Run(td);
    });
    TEST_ASSERT(!guard.succeeded);
    TEST_ASSERT(guard.stopped);
    TEST_ASSERT(guard.stop_reason.find("Deadlock") != std::string::npos);
    TEST_ASSERT(guard.stop_reason.find("deadlock_t") != std::string::npos);

    GetScheduler().Reset();
    std::printf("kernel_sched_probe passed.\n");
    return 0;
}
