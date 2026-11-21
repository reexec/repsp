#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/sha1.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define TEST_ASSERT(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::fflush(nullptr); \
            std::_Exit(1); \
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
constexpr uint32_t kAddrStealSig = 0x08808000u;
constexpr uint32_t kAddrStealLow = 0x08808010u;
constexpr uint32_t kAddrStealHi  = 0x08808020u;
constexpr uint32_t kAddrQueueMain = 0x08809000u;
constexpr uint32_t kAddrQueueW1   = 0x08809010u;
constexpr uint32_t kAddrQueueW2   = 0x08809020u;
constexpr uint32_t kAddrEvfMain   = 0x0880A000u;
constexpr uint32_t kAddrEvfW1     = 0x0880A010u;
constexpr uint32_t kAddrEvfW2     = 0x0880A020u;
constexpr uint32_t kAddrPreemptA  = 0x0880B000u;
constexpr uint32_t kAddrPreemptB  = 0x0880B010u;
constexpr uint32_t kAddrPreemptH  = 0x0880B020u;
constexpr uint32_t kAddrIntrMain  = 0x0880C000u;
constexpr uint32_t kAddrIntrIsr   = 0x0880C010u;
constexpr uint32_t kAddrIntrNest  = 0x0880C020u;
constexpr uint32_t kAddrIntrWaitMain = 0x0880C030u;
constexpr uint32_t kAddrIntrWaitIsr  = 0x0880C040u;
constexpr uint32_t kAddrDeepStack    = 0x0880E000u;
constexpr uint32_t kAddrStackOverflow = 0x0880E010u;
constexpr uint32_t kAddrHostWaitMain = 0x0880E020u;
constexpr uint32_t kAddrHostWaitHigh = 0x0880E030u;
constexpr uint32_t kAddrDelayMinWaiter = 0x0880E040u;
constexpr uint32_t kAddrDelayMinInspector = 0x0880E050u;
constexpr uint32_t kAddrVplMain = 0x0880E060u;
constexpr uint32_t kAddrVplWaiter = 0x0880E070u;
constexpr uint32_t kAddrPollTimeoutLow = 0x0880E080u;
constexpr uint32_t kAddrPollTimeoutHigh = 0x0880E090u;

int32_t g_vpl = 0;
uint32_t g_vpl_waiter_addr = 0;
int32_t g_poll_sema = 0;
int g_poll_high_stage = 0;

int32_t g_delay_min_waiter_uid = 0;
std::vector<int64_t> g_delay_min_observed_us;

std::mutex g_host_wait_mutex;
std::condition_variable g_host_wait_cv;
bool g_host_wait_high_ran = false;
bool g_host_wait_passed = false;
uint32_t g_host_wait_saved_s0 = 0;
int32_t g_host_wait_main_uid = 0;

volatile uintptr_t g_deep_stack_span = 0;
volatile uint64_t g_deep_stack_committed = 0;

uint32_t DeepStackTouch(int depth)
{
    volatile uint8_t frame[4096];
    frame[0] = static_cast<uint8_t>(depth & 0xFF);
    frame[4095] = static_cast<uint8_t>((depth * 3) & 0xFF);
    if (depth <= 0)
    {
        repsp::platform::HostStackInfo info{};
        if (repsp::platform::HostThread::GetCurrentThreadStackInfo(info))
        {
            g_deep_stack_span = info.stack_high - info.stack_low;
            g_deep_stack_committed = info.committed_bytes;
        }
        return frame[0] + frame[4095];
    }
    return DeepStackTouch(depth - 1) + frame[0];
}

int32_t g_steal_hi_tid = 0;

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
    case kAddrStealSig:
    {
        WriteStr(0x09000700u, "steal_hi");
        WriteStr(0x09000720u, "steal_lo");
        g_steal_hi_tid = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000700u, kAddrStealHi, 0x10, 0x4000, 0));
        int32_t lo = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000720u, kAddrStealLow, 0x30, 0x4000, 0));
        HleCall("sceKernelStartThread", static_cast<uint32_t>(g_steal_hi_tid), 0, 0); // runs & sleeps
        HleCall("sceKernelStartThread", static_cast<uint32_t>(lo), 0, 0);
        HleCall("sceKernelDelayThread", 1000); // let lo block on WaitSema
        g_log.push_back("sig:before_signal");
        HleCall("sceKernelSignalSema", static_cast<uint32_t>(g_sema), 1); // hands off to lo
        // Wake higher-priority thread right after SignalSema; must NOT steal sema from lo!
        HleCall("sceKernelWakeupThread", static_cast<uint32_t>(g_steal_hi_tid));
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(lo), 0);
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(g_steal_hi_tid), 0);
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrStealHi:
    {
        HleCall("sceKernelSleepThread");
        g_log.push_back("hi:try_wait");
        TEST_ASSERT(HleCall("sceKernelWaitSema", static_cast<uint32_t>(g_sema), 1, 0) == 0);
        g_log.push_back("hi:got");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrStealLow:
    {
        g_log.push_back("lo:wait");
        TEST_ASSERT(HleCall("sceKernelWaitSema", static_cast<uint32_t>(g_sema), 1, 0) == 0);
        g_log.push_back("lo:got");
        HleCall("sceKernelSignalSema", static_cast<uint32_t>(g_sema), 1);
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrQueueMain:
    {
        WriteStr(0x09000800u, "qw1");
        WriteStr(0x09000820u, "qw2");
        int32_t w1 = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000800u, kAddrQueueW1, 0x30, 0x4000, 0));
        int32_t w2 = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000820u, kAddrQueueW2, 0x25, 0x4000, 0));
        HleCall("sceKernelStartThread", static_cast<uint32_t>(w1), 0, 0);
        HleCall("sceKernelDelayThread", 500); // w1 enters wait queue first
        HleCall("sceKernelStartThread", static_cast<uint32_t>(w2), 0, 0);
        HleCall("sceKernelDelayThread", 500); // w2 enters wait queue second
        HleCall("sceKernelSignalSema", static_cast<uint32_t>(g_sema), 1);
        HleCall("sceKernelDelayThread", 500);
        HleCall("sceKernelSignalSema", static_cast<uint32_t>(g_sema), 1);
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(w1), 0);
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(w2), 0);
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrQueueW1:
    {
        TEST_ASSERT(HleCall("sceKernelWaitSema", static_cast<uint32_t>(g_sema), 1, 0) == 0);
        g_log.push_back("qw1:got");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrQueueW2:
    {
        TEST_ASSERT(HleCall("sceKernelWaitSema", static_cast<uint32_t>(g_sema), 1, 0) == 0);
        g_log.push_back("qw2:got");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrEvfMain:
    {
        // First test single-waiter (attr=0): second waiter must be rejected.
        WriteStr(0x09000900u, "evf_s");
        int32_t single_evf = static_cast<int32_t>(HleCall("sceKernelCreateEventFlag", 0x09000900u, 0, 0));
        WriteStr(0x09000920u, "ew1");
        WriteStr(0x09000940u, "ew2");
        g_evf = single_evf;
        int32_t sw1 = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000920u, kAddrEvfW1, 0x30, 0x4000, 0));
        HleCall("sceKernelStartThread", static_cast<uint32_t>(sw1), 0, 0);
        HleCall("sceKernelDelayThread", 500);
        int32_t second_res = static_cast<int32_t>(HleCall("sceKernelWaitEventFlag", static_cast<uint32_t>(single_evf), 0x1u, 0, 0, 0));
        TEST_ASSERT(second_res == kSceKernelErrorError);
        HleCall("sceKernelSetEventFlag", static_cast<uint32_t>(single_evf), 0x1u);
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(sw1), 0);

        // Next test WAITMULTIPLE (0x200) | WAITPRIO (0x100) + CLEAR (0x20) handoff order.
        WriteStr(0x09000960u, "evf_m");
        g_evf = static_cast<int32_t>(HleCall("sceKernelCreateEventFlag", 0x09000960u, 0x300u, 0));
        g_log.clear();
        int32_t mw1 = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000920u, kAddrEvfW1, 0x30, 0x4000, 0));
        int32_t mw2 = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000940u, kAddrEvfW2, 0x25, 0x4000, 0));
        HleCall("sceKernelStartThread", static_cast<uint32_t>(mw1), 0, 0);
        HleCall("sceKernelDelayThread", 500); // mw1 waits on 0x1
        HleCall("sceKernelStartThread", static_cast<uint32_t>(mw2), 0, 0);
        HleCall("sceKernelDelayThread", 500); // mw2 (higher prio) waits on 0x1 with CLEAR (0x20)
        // First SetEventFlag(0x1): mw2 runs first in prio queue, records out_bits=0x1, clears 0x1 -> mw1 stays waiting!
        HleCall("sceKernelSetEventFlag", static_cast<uint32_t>(g_evf), 0x1u);
        HleCall("sceKernelDelayThread", 500);
        g_log.push_back("after_set1");
        // Second SetEventFlag(0x1): now mw1 wakes and gets out_bits=0x1.
        HleCall("sceKernelSetEventFlag", static_cast<uint32_t>(g_evf), 0x1u);
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(mw1), 0);
        HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(mw2), 0);
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrEvfW1:
    {
        GetGuestMemory().WriteU32(0x09000980u, 0);
        TEST_ASSERT(HleCall("sceKernelWaitEventFlag", static_cast<uint32_t>(g_evf), 0x1u, 0, 0x09000980u, 0) == 0);
        TEST_ASSERT(GetGuestMemory().ReadU32(0x09000980u) == 0x1u);
        g_log.push_back("ew1:got");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrEvfW2:
    {
        GetGuestMemory().WriteU32(0x09000984u, 0);
        TEST_ASSERT(HleCall("sceKernelWaitEventFlag", static_cast<uint32_t>(g_evf), 0x1u, 0x20u, 0x09000984u, 0) == 0);
        TEST_ASSERT(GetGuestMemory().ReadU32(0x09000984u) == 0x1u);
        g_log.push_back("ew2:got");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrPreemptA:
    {
        WriteStr(0x09000A00u, "pb");
        WriteStr(0x09000A20u, "ph");
        int32_t tb = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000A00u, kAddrPreemptB, 0x20, 0x4000, 0));
        int32_t th = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000A20u, kAddrPreemptH, 0x10, 0x4000, 0));
        HleCall("sceKernelStartThread", static_cast<uint32_t>(tb), 0, 0); // same prio -> queued behind A
        HleCall("sceKernelStartThread", static_cast<uint32_t>(th), 0, 0); // higher prio -> preempts A!
        g_log.push_back("a:resumed");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrPreemptB:
    {
        g_log.push_back("b:run");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrPreemptH:
    {
        g_log.push_back("h:run");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrIntrMain:
    {
        g_cpu.gpr[16] = 0xCAFE1234u; // $s0 preserved across interrupt
        const uint32_t orig_sp = g_cpu.gpr[29];
        GetScheduler().QueueInterrupt(kAddrIntrIsr, 0x11u, 0x22u, 0x089256F0u);
        repsp_recomp_poll(); // safe point delivers interrupt
        TEST_ASSERT(g_cpu.gpr[16] == 0xCAFE1234u);
        TEST_ASSERT(g_cpu.gpr[29] == orig_sp);
        g_log.push_back("intr_main:done");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrIntrIsr:
    {
        TEST_ASSERT(GetScheduler().IsInInterrupt());
        TEST_ASSERT(g_cpu.gpr[29] == Scheduler::kInterruptStackTop);
        TEST_ASSERT(g_cpu.gpr[28] == 0x089256F0u);
        TEST_ASSERT(g_cpu.gpr[4] == 0x11u);
        TEST_ASSERT(g_cpu.gpr[5] == 0x22u);
        g_cpu.gpr[16] = 0xDEAD0000u; // clobber $s0 to verify save/restore
        // Nested interrupt queued inside handler must wait until outer handler finishes.
        GetScheduler().QueueInterrupt(kAddrIntrNest, 0x33u, 0x44u, 0x08920000u);
        repsp_recomp_poll(); // must NOT deliver kAddrIntrNest while in_interrupt_
        g_log.push_back("isr:outer");
        break;
    }
    case kAddrIntrNest:
    {
        TEST_ASSERT(GetScheduler().IsInInterrupt());
        TEST_ASSERT(g_cpu.gpr[28] == 0x08920000u);
        TEST_ASSERT(g_cpu.gpr[4] == 0x33u);
        g_log.push_back("isr:nested");
        break;
    }
    case kAddrIntrWaitMain:
    {
        GetScheduler().QueueInterrupt(kAddrIntrWaitIsr, 0, 0, 0);
        repsp_recomp_poll();
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrIntrWaitIsr:
    {
        HleCall("sceKernelSleepThread"); // forbidden inside interrupt handler
        break;
    }
    case kAddrVplMain:
    {
        auto& mem = GetGuestMemory();
        WriteStr(0x09000B00u, "test_vpl");
        WriteStr(0x09000B20u, "vpl_waiter");

        g_vpl = static_cast<int32_t>(HleCall("sceKernelCreateVpl", 0x09000B00u, 2u, 0u, 512u, 0u));
        TEST_ASSERT(g_vpl > 0);

        // ReferVplStatus before allocations
        TEST_ASSERT(HleCall("sceKernelReferVplStatus", static_cast<uint32_t>(g_vpl), 0x09000B40u) == 0);
        TEST_ASSERT(mem.ReadU32(0x09000B40u + 0x28u) == 512u);
        TEST_ASSERT(mem.ReadU32(0x09000B40u + 0x2Cu) == 512u);

        // Allocate two 256-byte blocks filling the 512-byte pool
        TEST_ASSERT(HleCall("sceKernelAllocateVpl", static_cast<uint32_t>(g_vpl), 256u, 0x09000B80u, 0u) == 0);
        TEST_ASSERT(HleCall("sceKernelAllocateVpl", static_cast<uint32_t>(g_vpl), 256u, 0x09000B84u, 0u) == 0);
        const uint32_t b1 = mem.ReadU32(0x09000B80u);
        const uint32_t b2 = mem.ReadU32(0x09000B84u);
        TEST_ASSERT(b1 != 0 && b2 != 0 && b1 != b2);
        TEST_ASSERT((b1 & 7u) == 0 && (b2 & 7u) == 0);

        // Exhaustion checks: TryAllocateVpl -> NO_MEMORY, zero-timeout AllocateVpl -> WAIT_TIMEOUT
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceKernelTryAllocateVpl", static_cast<uint32_t>(g_vpl), 16u, 0x09000B88u))
                    == kSceKernelErrorNoMemory);
        mem.WriteU32(0x09000B8Cu, 0u);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceKernelAllocateVpl", static_cast<uint32_t>(g_vpl), 16u, 0x09000B88u, 0x09000B8Cu))
                    == kSceKernelErrorWaitTimeout);

        // Spawn higher-priority waiter needing 384 bytes (requires coalescing b1 + b2)
        int32_t tw = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000B20u, kAddrVplWaiter, 0x18u, 0x4000u, 0u));
        TEST_ASSERT(tw > 0);
        TEST_ASSERT(HleCall("sceKernelStartThread", static_cast<uint32_t>(tw), 0u, 0u) == 0);
        TEST_ASSERT(g_vpl_waiter_addr == 0);

        // Free b1 (256 B): still not enough for 384 B
        TEST_ASSERT(HleCall("sceKernelFreeVpl", static_cast<uint32_t>(g_vpl), b1) == 0);
        TEST_ASSERT(g_vpl_waiter_addr == 0);

        // Free b2 (256 B): coalesces to 512 B, wakes waiter immediately (higher prio)
        TEST_ASSERT(HleCall("sceKernelFreeVpl", static_cast<uint32_t>(g_vpl), b2) == 0);
        TEST_ASSERT(HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(tw), 0u) == 0);
        TEST_ASSERT(g_vpl_waiter_addr != 0);

        // Pool is fully free again after waiter freed its block
        TEST_ASSERT(HleCall("sceKernelReferVplStatus", static_cast<uint32_t>(g_vpl), 0x09000B40u) == 0);
        TEST_ASSERT(mem.ReadU32(0x09000B40u + 0x2Cu) == 512u);

        TEST_ASSERT(HleCall("sceKernelDeleteVpl", static_cast<uint32_t>(g_vpl)) == 0);
        TEST_ASSERT(static_cast<int32_t>(HleCall("sceKernelDeleteVpl", static_cast<uint32_t>(g_vpl)))
                    == kSceKernelErrorUnknownVplid);
        g_log.push_back("vpl:done");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrVplWaiter:
    {
        auto& mem = GetGuestMemory();
        mem.WriteU32(0x09000B90u, 0u);
        TEST_ASSERT(HleCall("sceKernelAllocateVpl", static_cast<uint32_t>(g_vpl), 384u, 0x09000B90u, 0u) == 0);
        g_vpl_waiter_addr = mem.ReadU32(0x09000B90u);
        TEST_ASSERT(g_vpl_waiter_addr != 0);
        mem.WriteU32(g_vpl_waiter_addr, 0x12345678u);
        TEST_ASSERT(HleCall("sceKernelFreeVpl", static_cast<uint32_t>(g_vpl), g_vpl_waiter_addr) == 0);
        g_log.push_back("vpl_waiter:got");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrPollTimeoutLow:
    {
        WriteStr(0x09000C00u, "poll_high");
        WriteStr(0x09000C20u, "poll_sema");
        g_poll_sema = static_cast<int32_t>(HleCall("sceKernelCreateSema", 0x09000C20u, 0u, 0u, 1u, 0u));
        TEST_ASSERT(g_poll_sema > 0);

        int32_t th = static_cast<int32_t>(HleCall("sceKernelCreateThread", 0x09000C00u, kAddrPollTimeoutHigh, 18u, 0x4000u, 0u));
        TEST_ASSERT(th > 0);
        // StartThread preempts into high-priority thread, which immediately calls DelayThread(2000).
        TEST_ASSERT(HleCall("sceKernelStartThread", static_cast<uint32_t>(th), 0u, 0u) == 0);
        TEST_ASSERT(g_poll_high_stage == 1);

        // Suspend interrupts and spin past the 2 ms deadline calling Poll(): must NOT preempt.
        uint32_t intr_state = HleCall("sceKernelCpuSuspendIntr");
        auto spin_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(6);
        while (std::chrono::steady_clock::now() < spin_until)
        {
            repsp_recomp_poll();
            TEST_ASSERT(g_poll_high_stage == 1);
        }

        // Resume interrupts; the very first Poll() must preempt into high-priority thread.
        HleCall("sceKernelCpuResumeIntr", intr_state);
        repsp_recomp_poll();
        TEST_ASSERT(g_poll_high_stage == 2);

        // High thread is now in WaitSema with a 2 ms timeout; busy-poll until it times out.
        auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (g_poll_high_stage < 3 && std::chrono::steady_clock::now() < limit)
        {
            repsp_recomp_poll();
        }
        TEST_ASSERT(g_poll_high_stage == 3);

        TEST_ASSERT(HleCall("sceKernelWaitThreadEnd", static_cast<uint32_t>(th), 0u) == 0);
        TEST_ASSERT(HleCall("sceKernelDeleteSema", static_cast<uint32_t>(g_poll_sema)) == 0);
        g_log.push_back("poll_low:done");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrPollTimeoutHigh:
    {
        g_poll_high_stage = 1;
        TEST_ASSERT(HleCall("sceKernelDelayThread", 2000u) == 0);
        g_poll_high_stage = 2;
        g_log.push_back("poll_high:delay_woke");

        GetGuestMemory().WriteU32(0x09000C40u, 2000u);
        int32_t res = static_cast<int32_t>(
            HleCall("sceKernelWaitSema", static_cast<uint32_t>(g_poll_sema), 1u, 0x09000C40u));
        TEST_ASSERT(res == kSceKernelErrorWaitTimeout);
        g_poll_high_stage = 3;
        g_log.push_back("poll_high:sema_timeout");
        g_cpu.gpr[2] = 0;
        break;
    }
    case kAddrDeepStack:
    {
        g_cpu.gpr[2] = DeepStackTouch(600);
        break;
    }
    case kAddrStackOverflow:
    {
        g_cpu.gpr[2] = DeepStackTouch(3000);
        break;
    }
    case kAddrHostWaitMain:
    {
        g_cpu.gpr[16] = 0xCAFEBABEu;
        int32_t t_high = GetScheduler().CreateThread("host_wait_high", kAddrHostWaitHigh, 18, 0x4000, 0, 0);
        TEST_ASSERT(t_high > 0);
        // StartThread preempts into t_high immediately; t_high blocks on WaitType::kAudio
        // until woken from inside ScopedHostWait below.
        TEST_ASSERT(GetScheduler().StartThread(t_high, 0, 0) == 0);
        {
            repsp::runtime::Scheduler::ScopedHostWait host_wait(GetScheduler());
            std::thread waker([]()
            {
                GetScheduler().WakeAllWaiters(repsp::runtime::WaitType::kAudio, 0x7777u, 0);
            });
            std::unique_lock<std::mutex> lock(g_host_wait_mutex);
            g_host_wait_passed = g_host_wait_cv.wait_for(
                lock, std::chrono::milliseconds(1500), []() { return g_host_wait_high_ran; });
            waker.join();
        }
        g_host_wait_saved_s0 = g_cpu.gpr[16];
        break;
    }
    case kAddrHostWaitHigh:
    {
        TEST_ASSERT(GetScheduler().WaitCurrent(
                        repsp::runtime::WaitType::kAudio, 0x7777u, "host_wait_probe", nullptr, false) == 0);
        g_cpu.gpr[16] = 0xDEAD0000u;
        const auto snap = GetScheduler().GetThreadsSnapshot();
        bool found_host_wait = false;
        for (const auto& info : snap)
        {
            if (info.uid == g_host_wait_main_uid)
            {
                found_host_wait = (info.state == repsp::runtime::ThreadState::kWaiting &&
                                   info.wait_type == repsp::runtime::WaitType::kHostWait);
            }
        }
        TEST_ASSERT(found_host_wait);
        TEST_ASSERT(GetScheduler().WakeAllWaiters(repsp::runtime::WaitType::kHostWait, 0, 0) == 0);
        TEST_ASSERT(GetScheduler().WakeAllWaiters(repsp::runtime::WaitType::kCustom, 0, 0) == 0);
        TEST_ASSERT(!GetScheduler().WakeThreadFromWait(g_host_wait_main_uid, 0));
        TEST_ASSERT(GetScheduler().TerminateThread(g_host_wait_main_uid, false) < 0);
        TEST_ASSERT(GetScheduler().DeleteThread(g_host_wait_main_uid) < 0);
        {
            std::lock_guard<std::mutex> lock(g_host_wait_mutex);
            g_host_wait_high_ran = true;
        }
        g_host_wait_cv.notify_all();
        break;
    }
    case kAddrDelayMinWaiter:
    {
        TEST_ASSERT(HleCall("sceKernelDelayThread", 0u) == 0u);
        TEST_ASSERT(HleCall("sceKernelDelayThread", 10u) == 0u);
        TEST_ASSERT(HleCall("sceKernelDelayThread", 5000u) == 0u);
        TEST_ASSERT(HleCall("sceKernelDelayThreadCB", 0u) == 0u);
        TEST_ASSERT(HleCall("sceKernelDelayThreadCB", 10u) == 0u);
        TEST_ASSERT(HleCall("sceKernelDelayThreadCB", 5000u) == 0u);
        break;
    }
    case kAddrDelayMinInspector:
    {
        for (int i = 0; i < 6; ++i)
        {
            bool has_timeout = false;
            int64_t duration_us = -1;
            TEST_ASSERT(GetScheduler().GetThreadWaitDeadlineForTest(
                g_delay_min_waiter_uid, &has_timeout, &duration_us));
            TEST_ASSERT(has_timeout);
            g_delay_min_observed_us.push_back(duration_us);
            TEST_ASSERT(GetScheduler().WakeAllWaiters(
                            repsp::runtime::WaitType::kDelay, 0, repsp::runtime::Scheduler::kWaitTimeout) == 1u);
            GetScheduler().YieldCurrent();
        }
        break;
    }
    default:
        TEST_ASSERT(false);
    }
}

const repsp_guest_cpu_ops g_fake_ops = {
    sizeof(FakeCpuState), FakeSave, FakeRestore, FakeGetGpr, FakeSetGpr, FakeCallGuest, nullptr, nullptr
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

    // 7. Sema signal-time handoff (higher-priority thread cannot steal after SignalSema)
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    WriteStr(0x09000740u, "sema_steal");
    g_sema = static_cast<int32_t>(HleCall("sceKernelCreateSema", 0x09000740u, 0, 0, 1));
    int32_t t_steal = GetScheduler().CreateThread("steal_sig", kAddrStealSig, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(t_steal);
    TEST_ASSERT((g_log == std::vector<std::string>{ "lo:wait", "sig:before_signal", "hi:try_wait", "lo:got", "hi:got" }));

    // 8a. Sema FIFO wait queue (attr=0): qw1 (prio 0x30) wakes before qw2 (prio 0x25)
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    WriteStr(0x09000840u, "sema_fifo");
    g_sema = static_cast<int32_t>(HleCall("sceKernelCreateSema", 0x09000840u, 0, 0, 1));
    int32_t t_qfifo = GetScheduler().CreateThread("q_fifo", kAddrQueueMain, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(t_qfifo);
    TEST_ASSERT((g_log == std::vector<std::string>{ "qw1:got", "qw2:got" }));

    // 8b. Sema Priority wait queue (attr=0x100): qw2 (prio 0x25) wakes before qw1 (prio 0x30)
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    WriteStr(0x09000860u, "sema_prio");
    g_sema = static_cast<int32_t>(HleCall("sceKernelCreateSema", 0x09000860u, 0x100u, 0, 1));
    int32_t t_qprio = GetScheduler().CreateThread("q_prio", kAddrQueueMain, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(t_qprio);
    TEST_ASSERT((g_log == std::vector<std::string>{ "qw2:got", "qw1:got" }));

    // 9. EventFlag WAITMULTIPLE refusal, 0x100 prio order, and CLEAR handoff
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    int32_t t_evf = GetScheduler().CreateThread("evf_main", kAddrEvfMain, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(t_evf);
    TEST_ASSERT((g_log == std::vector<std::string>{ "ew2:got", "after_set1", "ew1:got" }));

    // 10. Preempted thread remains at front of its priority ready queue
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    int32_t t_pa = GetScheduler().CreateThread("pa", kAddrPreemptA, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(t_pa);
    TEST_ASSERT((g_log == std::vector<std::string>{ "h:run", "a:resumed", "b:run" }));

    // 11. Interrupt delivery: dedicated stack, $gp/$a0/$a1, register save/restore, no nested delivery
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    int32_t t_intr = GetScheduler().CreateThread("intr_main", kAddrIntrMain, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(t_intr);
    TEST_ASSERT((g_log == std::vector<std::string>{ "isr:outer", "isr:nested", "intr_main:done" }));

    // 12. Waiting inside an interrupt handler triggers RaiseStop
    kernel::Register(kernel::KernelConfig{});
    int32_t t_iwait = GetScheduler().CreateThread("intr_wait", kAddrIntrWaitMain, 0x20, 0x4000, 0, 0);
    auto intr_guard = repsp::platform::GuardedRun([&]()
    {
        GetScheduler().Run(t_iwait);
    });
    TEST_ASSERT(!intr_guard.succeeded);
    TEST_ASSERT(intr_guard.stopped);
    TEST_ASSERT(intr_guard.stop_reason.find("interrupt") != std::string::npos);

    // 13. Explicit 8 MiB reserved host stack for guest threads and per-thread high-water stats
    TEST_ASSERT(GetScheduler().GetGuestHostStackReserveBytes() ==
                repsp::runtime::Scheduler::kDefaultGuestHostStackReserveBytes);
    GetScheduler().SetGuestHostStackReserveBytes(16ull * 1024ull * 1024ull);
    TEST_ASSERT(GetScheduler().GetGuestHostStackReserveBytes() == 16ull * 1024ull * 1024ull);
    GetScheduler().SetGuestHostStackReserveBytes(
        repsp::runtime::Scheduler::kDefaultGuestHostStackReserveBytes);

    kernel::Register(kernel::KernelConfig{});
    g_deep_stack_span = 0;
    g_deep_stack_committed = 0;
    int32_t t_deep = GetScheduler().CreateThread("deep_stack", kAddrDeepStack, 0x20, 0x4000, 0, 0);
    GetScheduler().Run(t_deep);
    TEST_ASSERT(g_deep_stack_span >= repsp::runtime::Scheduler::kDefaultGuestHostStackReserveBytes);
    TEST_ASSERT(g_deep_stack_committed >= 2ull * 1024ull * 1024ull);
    TEST_ASSERT(g_deep_stack_committed < repsp::runtime::Scheduler::kDefaultGuestHostStackReserveBytes);
    {
        const auto snap = GetScheduler().GetThreadsSnapshot();
        bool found_deep = false;
        for (const auto& info : snap)
        {
            if (info.uid == t_deep)
            {
                found_deep = true;
                TEST_ASSERT(info.host_stack_reserve_bytes ==
                            repsp::runtime::Scheduler::kDefaultGuestHostStackReserveBytes);
                TEST_ASSERT(info.host_stack_high_water_bytes >= 2ull * 1024ull * 1024ull);
            }
        }
        TEST_ASSERT(found_deep);
        const std::string stats = GetScheduler().FormatHostStackStats();
        TEST_ASSERT(stats.find("deep_stack") != std::string::npos);
        TEST_ASSERT(stats.find("high_water=") != std::string::npos);
    }

#if defined(_WIN32)
    // 14. Stack overflow past the 8 MiB default reservation is still caught as a stop report
    kernel::Register(kernel::KernelConfig{});
    int32_t t_ovf = GetScheduler().CreateThread("stack_ovf", kAddrStackOverflow, 0x20, 0x4000, 0, 0);
    auto ovf_guard = repsp::platform::GuardedRun([&]()
    {
        GetScheduler().Run(t_ovf);
    });
    TEST_ASSERT(!ovf_guard.succeeded);
    TEST_ASSERT(ovf_guard.stopped);
    TEST_ASSERT(ovf_guard.stop_reason.find("Stack overflow") != std::string::npos);
#endif

    // 15. ScopedHostWait yields guest CPU during host blocking wait and preserves CPU context
    kernel::Register(kernel::KernelConfig{});
    g_host_wait_high_ran = false;
    g_host_wait_passed = false;
    g_host_wait_saved_s0 = 0;
    int32_t t_host_wait = GetScheduler().CreateThread("host_wait_main", kAddrHostWaitMain, 32, 0x4000, 0, 0);
    TEST_ASSERT(t_host_wait > 0);
    g_host_wait_main_uid = t_host_wait;
    GetScheduler().Run(t_host_wait);
    TEST_ASSERT(g_host_wait_high_ran);
    TEST_ASSERT(g_host_wait_passed);
    TEST_ASSERT(g_host_wait_saved_s0 == 0xCAFEBABEu);

    // 16. sceKernelDelayThread / sceKernelDelayThreadCB enforce a 200 us minimum deadline
    kernel::Register(kernel::KernelConfig{});
    g_delay_min_observed_us.clear();
    int32_t t_dwait = GetScheduler().CreateThread("delay_waiter", kAddrDelayMinWaiter, 32, 0x4000, 0, 0);
    int32_t t_dinsp = GetScheduler().CreateThread("delay_insp", kAddrDelayMinInspector, 33, 0x4000, 0, 0);
    TEST_ASSERT(t_dwait > 0 && t_dinsp > 0);
    g_delay_min_waiter_uid = t_dwait;
    TEST_ASSERT(GetScheduler().StartThread(t_dinsp, 0, 0) == 0);
    GetScheduler().Run(t_dwait);
    TEST_ASSERT((g_delay_min_observed_us == std::vector<int64_t>{ 200, 200, 5000, 200, 200, 5000 }));

    // 17. VPL create, allocate, exhaustion, coalescing free, wait/wake handoff, delete
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    g_vpl = 0;
    g_vpl_waiter_addr = 0;
    int32_t t_vpl = GetScheduler().CreateThread("vpl_main", kAddrVplMain, 0x20, 0x4000, 0, 0);
    TEST_ASSERT(t_vpl > 0);
    GetScheduler().Run(t_vpl);
    TEST_ASSERT((g_log == std::vector<std::string>{ "vpl_waiter:got", "vpl:done" }));

    // 18. Scheduler::Poll() expires timed waits (DelayThread & WaitSema timeout) during busy loop
    //     and respects CpuSuspendIntr until resumed.
    kernel::Register(kernel::KernelConfig{});
    g_log.clear();
    g_poll_sema = 0;
    g_poll_high_stage = 0;
    int32_t t_poll_low = GetScheduler().CreateThread("poll_low", kAddrPollTimeoutLow, 32, 0x4000, 0, 0);
    TEST_ASSERT(t_poll_low > 0);
    GetScheduler().Run(t_poll_low);
    TEST_ASSERT((g_log == std::vector<std::string>{ "poll_high:delay_woke", "poll_high:sema_timeout", "poll_low:done" }));

    GetScheduler().Reset();
    std::printf("kernel_sched_probe passed.\n");
    return 0;
}
