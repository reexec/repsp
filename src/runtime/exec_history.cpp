#include "repsp/runtime/exec_history.h"
#include "repsp/platform/crash_handler.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/vblank_clock.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace repsp::runtime
{

namespace
{

constexpr size_t kMaxTrackedThreads = 64;
constexpr size_t kGuestEntryRingSize = 64;
constexpr size_t kHleCallRingSize = 32;

struct HleCallRecord
{
    uint32_t nid = 0;
    uint32_t a0 = 0;
    uint32_t a1 = 0;
    uint32_t a2 = 0;
    uint32_t a3 = 0;
    uint32_t ret = 0;
    bool completed = false;
    char name[48]{};
};

struct ThreadExecHistory
{
    std::atomic<bool> in_use{false};
    std::atomic<uint64_t> host_tid{0};
    std::atomic<int32_t> guest_uid{0};
    std::atomic<int32_t> guest_priority{0};
    char guest_name[64]{};

    uint32_t guest_entries[kGuestEntryRingSize]{};
    uint32_t guest_entry_count = 0;

    HleCallRecord hle_calls[kHleCallRingSize]{};
    uint32_t hle_call_count = 0;
};

struct ThreadSnapshotSlot
{
    std::atomic<bool> valid{false};
    int32_t uid = 0;
    int32_t priority = 0;
    int32_t wait_target_uid = 0;
    char name[48]{};
    char state_str[24]{};
    char wait_type_str[24]{};
    char wait_detail[48]{};
};

ThreadExecHistory g_histories[kMaxTrackedThreads];
ThreadSnapshotSlot g_thread_snapshots[kMaxTrackedThreads];
thread_local ThreadExecHistory* t_history = nullptr;

std::atomic<uint64_t> g_mutex_owner_host_tid{0};
std::atomic<int32_t> g_mutex_owner_guest_uid{0};
char g_last_stop_reason[256]{};
std::atomic<bool> g_provider_registered{false};

ThreadExecHistory* AcquireHistorySlot()
{
    if (t_history != nullptr)
    {
        return t_history;
    }
    for (size_t i = 0; i < kMaxTrackedThreads; ++i)
    {
        bool expected = false;
        if (g_histories[i].in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            g_histories[i].host_tid.store(platform::GetCurrentHostThreadId(), std::memory_order_relaxed);
            g_histories[i].guest_uid.store(0, std::memory_order_relaxed);
            g_histories[i].guest_priority.store(0, std::memory_order_relaxed);
            g_histories[i].guest_name[0] = '\0';
            g_histories[i].guest_entry_count = 0;
            g_histories[i].hle_call_count = 0;
            t_history = &g_histories[i];
            return t_history;
        }
    }
    return nullptr;
}

const ThreadExecHistory* FindHistoryByHostTid(uint64_t host_tid)
{
    for (size_t i = 0; i < kMaxTrackedThreads; ++i)
    {
        if (g_histories[i].in_use.load(std::memory_order_acquire) &&
            g_histories[i].host_tid.load(std::memory_order_relaxed) == host_tid)
        {
            return &g_histories[i];
        }
    }
    return nullptr;
}

void WriteCrashStateReport(void* /*user_data*/, platform::CrashTextBuffer* out)
{
    if (out == nullptr)
    {
        return;
    }

    const uint64_t cur_tid = platform::GetCurrentHostThreadId();
    const uint64_t lock_tid = g_mutex_owner_host_tid.load(std::memory_order_relaxed);
    const int32_t lock_uid = g_mutex_owner_guest_uid.load(std::memory_order_relaxed);

    out->AppendFormat("stop_reason: %s\n", g_last_stop_reason[0] ? g_last_stop_reason : "(none)");
    out->AppendFormat("scheduler_mutex_owner_host_tid: %llu\n", static_cast<unsigned long long>(lock_tid));
    out->AppendFormat("scheduler_mutex_owner_guest_uid: %d\n", lock_uid);
    out->AppendFormat("vblank_count: %llu\n", static_cast<unsigned long long>(GetVblankClock().Count()));

    const ThreadExecHistory* hist = FindHistoryByHostTid(cur_tid);
    if (hist != nullptr)
    {
        out->AppendFormat(
            "crashing_guest_thread: uid=%d name=%s priority=%d\n",
            hist->guest_uid.load(std::memory_order_relaxed),
            hist->guest_name[0] ? hist->guest_name : "(none)",
            hist->guest_priority.load(std::memory_order_relaxed));
    }
    else
    {
        out->Append("crashing_guest_thread: (none)\n");
    }

    const repsp_guest_cpu_ops* ops = GetScheduler().GetCpuOps();
    if (ops != nullptr && ops->get_gpr != nullptr)
    {
        out->Append("guest_registers:\n");
        for (int r = 0; r < 32; r += 4)
        {
            out->AppendFormat(
                "  r%02d=0x%08X r%02d=0x%08X r%02d=0x%08X r%02d=0x%08X\n",
                r, ops->get_gpr(r),
                r + 1, ops->get_gpr(r + 1),
                r + 2, ops->get_gpr(r + 2),
                r + 3, ops->get_gpr(r + 3));
        }
    }

    if (hist != nullptr)
    {
        const uint32_t entry_total = hist->guest_entry_count;
        const uint32_t entry_avail = (entry_total < kGuestEntryRingSize) ? entry_total : static_cast<uint32_t>(kGuestEntryRingSize);
        out->AppendFormat("recent_guest_entries (count=%u):\n", entry_avail);
        for (uint32_t i = 0; i < entry_avail; ++i)
        {
            const uint32_t idx = (entry_total - 1u - i) & static_cast<uint32_t>(kGuestEntryRingSize - 1);
            out->AppendFormat("  [-%u] 0x%08X\n", i, hist->guest_entries[idx]);
        }

        const uint32_t hle_total = hist->hle_call_count;
        const uint32_t hle_avail = (hle_total < kHleCallRingSize) ? hle_total : static_cast<uint32_t>(kHleCallRingSize);
        out->AppendFormat("recent_hle_calls (count=%u):\n", hle_avail);
        for (uint32_t i = 0; i < hle_avail; ++i)
        {
            const uint32_t idx = (hle_total - 1u - i) & static_cast<uint32_t>(kHleCallRingSize - 1);
            const HleCallRecord& rec = hist->hle_calls[idx];
            out->AppendFormat(
                "  [-%u] 0x%08X %s(0x%08X, 0x%08X, 0x%08X, 0x%08X) -> %s (0x%08X)\n",
                i, rec.nid, rec.name[0] ? rec.name : "?",
                rec.a0, rec.a1, rec.a2, rec.a3,
                rec.completed ? "done" : "in_progress",
                rec.ret);
        }
    }

    out->Append("scheduler_threads:\n");
    for (size_t i = 0; i < kMaxTrackedThreads; ++i)
    {
        if (!g_thread_snapshots[i].valid.load(std::memory_order_acquire))
        {
            continue;
        }
        const auto& slot = g_thread_snapshots[i];
        out->AppendFormat(
            "  uid=%d name=%s prio=%d state=%s wait=%s target=%d detail=%s\n",
            slot.uid,
            slot.name[0] ? slot.name : "?",
            slot.priority,
            slot.state_str[0] ? slot.state_str : "?",
            slot.wait_type_str[0] ? slot.wait_type_str : "None",
            slot.wait_target_uid,
            slot.wait_detail[0] ? slot.wait_detail : "-");
    }
}

} // namespace

void NoteGuestEntry(uint32_t addr)
{
    ThreadExecHistory* h = t_history;
    if (h == nullptr)
    {
        h = AcquireHistorySlot();
        if (h == nullptr)
        {
            return;
        }
    }
    const uint32_t idx = h->guest_entry_count++ & static_cast<uint32_t>(kGuestEntryRingSize - 1);
    h->guest_entries[idx] = addr;
}

void NoteHleCallBegin(uint32_t nid, const char* name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    ThreadExecHistory* h = t_history;
    if (h == nullptr)
    {
        h = AcquireHistorySlot();
        if (h == nullptr)
        {
            return;
        }
    }
    const uint32_t idx = h->hle_call_count++ & static_cast<uint32_t>(kHleCallRingSize - 1);
    HleCallRecord& rec = h->hle_calls[idx];
    rec.nid = nid;
    rec.a0 = a0;
    rec.a1 = a1;
    rec.a2 = a2;
    rec.a3 = a3;
    rec.ret = 0;
    rec.completed = false;
    if (name != nullptr)
    {
        std::snprintf(rec.name, sizeof(rec.name), "%s", name);
    }
    else
    {
        rec.name[0] = '\0';
    }
}

void NoteHleCallEnd(uint32_t ret)
{
    ThreadExecHistory* h = t_history;
    if (h == nullptr || h->hle_call_count == 0)
    {
        return;
    }
    const uint32_t idx = (h->hle_call_count - 1u) & static_cast<uint32_t>(kHleCallRingSize - 1);
    h->hle_calls[idx].ret = ret;
    h->hle_calls[idx].completed = true;
}

void SetCurrentThreadExecInfo(uint64_t host_tid, int32_t guest_uid, const char* name, int32_t priority)
{
    ThreadExecHistory* h = AcquireHistorySlot();
    if (h == nullptr)
    {
        return;
    }
    h->host_tid.store(host_tid, std::memory_order_relaxed);
    h->guest_uid.store(guest_uid, std::memory_order_relaxed);
    h->guest_priority.store(priority, std::memory_order_relaxed);
    if (name != nullptr)
    {
        std::snprintf(h->guest_name, sizeof(h->guest_name), "%s", name);
    }
    else
    {
        h->guest_name[0] = '\0';
    }
}

void ClearCurrentThreadExecInfo()
{
    if (t_history != nullptr)
    {
        t_history->guest_uid.store(0, std::memory_order_relaxed);
        t_history->in_use.store(false, std::memory_order_release);
        t_history = nullptr;
    }
}

void SetCrashStopReason(const char* reason)
{
    if (reason != nullptr)
    {
        std::snprintf(g_last_stop_reason, sizeof(g_last_stop_reason), "%s", reason);
    }
    else
    {
        g_last_stop_reason[0] = '\0';
    }
}

void SetCrashSchedulerMutexOwner(uint64_t host_tid, int32_t guest_uid)
{
    g_mutex_owner_host_tid.store(host_tid, std::memory_order_relaxed);
    g_mutex_owner_guest_uid.store(guest_uid, std::memory_order_relaxed);
}

void ClearCrashThreadSnapshots()
{
    for (size_t i = 0; i < kMaxTrackedThreads; ++i)
    {
        g_thread_snapshots[i].valid.store(false, std::memory_order_release);
    }
}

void UpdateCrashThreadSnapshot(
    int32_t uid,
    const char* name,
    int32_t priority,
    const char* state_str,
    const char* wait_type_str,
    int32_t wait_target_uid,
    const char* wait_detail)
{
    size_t free_idx = kMaxTrackedThreads;
    for (size_t i = 0; i < kMaxTrackedThreads; ++i)
    {
        if (g_thread_snapshots[i].valid.load(std::memory_order_acquire))
        {
            if (g_thread_snapshots[i].uid == uid)
            {
                free_idx = i;
                break;
            }
        }
        else if (free_idx == kMaxTrackedThreads)
        {
            free_idx = i;
        }
    }
    if (free_idx >= kMaxTrackedThreads)
    {
        return;
    }
    auto& slot = g_thread_snapshots[free_idx];
    slot.uid = uid;
    slot.priority = priority;
    slot.wait_target_uid = wait_target_uid;
    std::snprintf(slot.name, sizeof(slot.name), "%s", name ? name : "");
    std::snprintf(slot.state_str, sizeof(slot.state_str), "%s", state_str ? state_str : "");
    std::snprintf(slot.wait_type_str, sizeof(slot.wait_type_str), "%s", wait_type_str ? wait_type_str : "None");
    std::snprintf(slot.wait_detail, sizeof(slot.wait_detail), "%s", wait_detail ? wait_detail : "");
    slot.valid.store(true, std::memory_order_release);
}

void RegisterRuntimeCrashProvider()
{
    if (!g_provider_registered.exchange(true))
    {
        platform::RegisterCrashStateProvider(WriteCrashStateReport, nullptr);
    }
}

} // namespace repsp::runtime

extern "C" void repsp_note_guest_entry(uint32_t addr)
{
    repsp::runtime::NoteGuestEntry(addr);
}
