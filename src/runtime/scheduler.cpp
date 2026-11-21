#include "repsp/runtime/scheduler.h"

#include "repsp/platform/crash_handler.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/runtime/exec_history.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/user_partition.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace repsp::runtime
{

namespace
{

constexpr const char* kInternalThreadExitStop = "__repsp_internal_thread_exit__";

// Tracks the guest thread executing on the current host thread so external host
// threads (vblank/audio clocks) never invoke YieldIfPreempted on themselves.
thread_local KernelUid t_current_thread_uid = 0;

constexpr int32_t kErrUnknownThid = static_cast<int32_t>(0x80020198u);       // SCE_KERNEL_ERROR_UNKNOWN_THID (uOFW)
constexpr int32_t kErrUnknownCbid = static_cast<int32_t>(0x8002019Du);       // SCE_KERNEL_ERROR_UNKNOWN_CBID (uOFW)
constexpr int32_t kErrIllegalPrio = static_cast<int32_t>(0x800201A3u);       // SCE_KERNEL_ERROR_ILLEGAL_PRIORITY (uOFW)
constexpr int32_t kErrIllegalStackSize = static_cast<int32_t>(0x800201A4u);  // SCE_KERNEL_ERROR_ILLEGAL_STACK_SIZE (uOFW)
constexpr int32_t kErrNotDormant = static_cast<int32_t>(0x8002019Fu);        // SCE_KERNEL_ERROR_NOT_DORMANT (uOFW)
constexpr int32_t kErrThreadDormant = static_cast<int32_t>(0x800201A0u);     // SCE_KERNEL_ERROR_DORMANT (uOFW)
constexpr int32_t kErrNoMemory = static_cast<int32_t>(0x800200D9u);          // SCE_KERNEL_ERROR_NO_MEMORY (uOFW)
constexpr int32_t kErrIllegalContext = static_cast<int32_t>(0x80020064u);    // SCE_KERNEL_ERROR_ILLEGAL_CONTEXT (uOFW)

constexpr int kRegV0 = 2;
constexpr int kRegA0 = 4;
constexpr int kRegA1 = 5;
constexpr int kRegA2 = 6;
constexpr int kRegA3 = 7;
constexpr int kRegK0 = 26;
constexpr int kRegGP = 28;
constexpr int kRegSP = 29;
constexpr int kRegRA = 31;

uint32_t AlignUp(uint32_t val, uint32_t align)
{
    return (val + align - 1u) & ~(align - 1u);
}

const char* ThreadStateName(ThreadState st)
{
    switch (st)
    {
    case ThreadState::kDormant: return "DORMANT";
    case ThreadState::kReady: return "READY";
    case ThreadState::kRunning: return "RUNNING";
    case ThreadState::kWaiting: return "WAITING";
    case ThreadState::kSuspended: return "SUSPENDED";
    case ThreadState::kDead: return "DEAD";
    default: return "UNKNOWN";
    }
}

const char* WaitTypeName(WaitType wt)
{
    switch (wt)
    {
    case WaitType::kNone: return "none";
    case WaitType::kThreadEnd: return "ThreadEnd";
    case WaitType::kSleep: return "Sleep";
    case WaitType::kDelay: return "Delay";
    case WaitType::kSema: return "Sema";
    case WaitType::kEventFlag: return "EventFlag";
    case WaitType::kAsyncIo: return "AsyncIo";
    case WaitType::kUmd: return "Umd";
    case WaitType::kAudio: return "Audio";
    case WaitType::kCustom: return "Custom";
    case WaitType::kHostWait: return "host wait";
    default: return "Unknown";
    }
}

void RaiseStopIfShuttingDownLocked(bool stopping, std::unique_lock<TrackedMutex>& lock)
{
    if (stopping && t_current_thread_uid != 0)
    {
        lock.unlock();
        platform::RaiseStop(kInternalThreadExitStop);
    }
}

size_t WakeLatencyBucketIndex(uint64_t us)
{
    if (us < 100u) return 0;
    if (us < 500u) return 1;
    if (us < 1000u) return 2;
    if (us < 2000u) return 3;
    if (us < 5000u) return 4;
    if (us < 10000u) return 5;
    if (us < 20000u) return 6;
    if (us < 50000u) return 7;
    return 8;
}

size_t PriorityBucketIndex(int32_t prio)
{
    if (prio < 16) return 0;
    if (prio < 32) return 1;
    if (prio < 64) return 2;
    return 3;
}

void RecordWakeBucket(WakeLatencyBucketStats& b, size_t bucket, uint64_t us)
{
    ++b.count;
    b.total_us += us;
    if (us > b.max_us)
    {
        b.max_us = us;
    }
    ++b.hist[bucket];
}

} // namespace

void TrackedMutex::lock()
{
    const uint64_t self_tid = platform::GetCurrentHostThreadId();
    if (owner_host_tid.load(std::memory_order_acquire) == self_tid)
    {
        const char* reason = "Scheduler mutex re-lock by same host thread";
        SetCrashStopReason(reason);
        platform::WriteLogFileLine("FATAL", reason);
        std::fprintf(stderr, "[FATAL] %s\n", reason);
        std::fflush(stderr);
        std::abort();
    }
    mutex.lock();
    owner_host_tid.store(self_tid, std::memory_order_release);
    SetCrashSchedulerMutexOwner(self_tid, t_current_thread_uid);
}

void TrackedMutex::unlock()
{
    owner_host_tid.store(0, std::memory_order_release);
    SetCrashSchedulerMutexOwner(0, 0);
    mutex.unlock();
}

bool TrackedMutex::try_lock()
{
    const uint64_t self_tid = platform::GetCurrentHostThreadId();
    if (owner_host_tid.load(std::memory_order_acquire) == self_tid)
    {
        return false;
    }
    if (!mutex.try_lock())
    {
        return false;
    }
    owner_host_tid.store(self_tid, std::memory_order_release);
    SetCrashSchedulerMutexOwner(self_tid, t_current_thread_uid);
    return true;
}

bool TrackedMutex::IsHeldByCurrentThread() const
{
    const uint64_t self_tid = platform::GetCurrentHostThreadId();
    return self_tid != 0 && owner_host_tid.load(std::memory_order_acquire) == self_tid;
}

bool TrackedMutex::UnlockIfHeldByCurrentThread()
{
    if (!IsHeldByCurrentThread())
    {
        return false;
    }
    unlock();
    return true;
}

void Scheduler::Reset()
{
    std::vector<std::shared_ptr<GuestThread>> old_threads;
    {
        std::unique_lock<TrackedMutex> lock(mutex_);
        shutdown_ = true;
        running_ = false;
        cv_.notify_all();
        old_threads.swap(threads_);
        ready_queue_.clear();
        pending_interrupts_.clear();
        active_thread_uid_ = 0;
        intr_enabled_ = true;
        in_interrupt_ = false;
        fatal_stop_ = false;
        fatal_stop_reason_.clear();
        external_wait_sources_ = 0;
        timed_waiter_count_ = 0;
    }

    for (auto& t : old_threads)
    {
        if (t->host_thread)
        {
            t->host_thread->Join();
            t->host_thread.reset();
        }
        GetKernelObjects().Remove(t->uid, KernelObjectType::kThread);
    }

    std::lock_guard<TrackedMutex> lock(mutex_);
    shutdown_ = false;
    const char* env = std::getenv("REPSP_SCHED_STATS");
    wake_latency_enabled_ = (env != nullptr && std::strcmp(env, "1") == 0);
    wake_stats_ = WakeLatencyStats{};
    wake_stats_.enabled = wake_latency_enabled_;
}

void Scheduler::SetCpuOps(const repsp_guest_cpu_ops* ops)
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    ops_ = ops;
}

std::shared_ptr<Scheduler::GuestThread> Scheduler::FindThreadLocked(KernelUid uid) const
{
    for (const auto& t : threads_)
    {
        if (t->uid == uid)
        {
            return t;
        }
    }
    return nullptr;
}

void Scheduler::RemoveFromReadyQueueLocked(KernelUid uid)
{
    ready_queue_.erase(std::remove(ready_queue_.begin(), ready_queue_.end(), uid), ready_queue_.end());
}

void Scheduler::MakeReadyLocked(const std::shared_ptr<GuestThread>& t, bool at_front)
{
    if (!t || t->state == ThreadState::kDead)
    {
        return;
    }
    if (wake_latency_enabled_ && t->state == ThreadState::kWaiting)
    {
        t->has_wake_stamp = true;
        t->wake_wait_type = t->wait_type;
        t->wake_time = std::chrono::steady_clock::now();
    }
    t->state = ThreadState::kReady;
    t->wait_type = WaitType::kNone;
    t->wait_target_uid = 0;
    t->wait_detail.clear();
    if (t->has_timeout)
    {
        t->has_timeout = false;
        if (timed_waiter_count_ > 0)
        {
            --timed_waiter_count_;
        }
    }
    auto it = std::find(ready_queue_.begin(), ready_queue_.end(), t->uid);
    if (it != ready_queue_.end())
    {
        ready_queue_.erase(it);
    }
    if (at_front)
    {
        ready_queue_.insert(ready_queue_.begin(), t->uid);
    }
    else
    {
        ready_queue_.push_back(t->uid);
    }
}

std::shared_ptr<Scheduler::GuestThread> Scheduler::PickNextReadyLocked()
{
    std::shared_ptr<GuestThread> best;
    size_t best_index = 0;
    for (size_t i = 0; i < ready_queue_.size(); ++i)
    {
        auto t = FindThreadLocked(ready_queue_[i]);
        if (!t || t->state != ThreadState::kReady)
        {
            continue;
        }
        if (!best || t->current_priority < best->current_priority)
        {
            best = t;
            best_index = i;
        }
    }
    if (best)
    {
        ready_queue_.erase(ready_queue_.begin() + static_cast<ptrdiff_t>(best_index));
    }
    return best;
}

bool Scheduler::HasHigherPriorityReadyLocked(int32_t priority) const
{
    for (KernelUid uid : ready_queue_)
    {
        auto t = FindThreadLocked(uid);
        if (t && t->state == ThreadState::kReady && t->current_priority < priority)
        {
            return true;
        }
    }
    return false;
}

void Scheduler::UpdateCurrentHostStackHighWaterLocked(const std::shared_ptr<GuestThread>& t)
{
    if (!t)
    {
        return;
    }
    platform::HostStackInfo info{};
    if (platform::HostThread::GetCurrentThreadStackInfo(info))
    {
        if (info.committed_bytes > t->host_stack_high_water_bytes)
        {
            t->host_stack_high_water_bytes = info.committed_bytes;
        }
    }
}

void Scheduler::SetGuestHostStackReserveBytes(size_t bytes)
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    guest_host_stack_reserve_bytes_ = bytes;
}

size_t Scheduler::GetGuestHostStackReserveBytes() const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    return guest_host_stack_reserve_bytes_;
}

void Scheduler::FreeThreadStackLocked(const std::shared_ptr<GuestThread>& t)
{
    if (t && t->stack_block_uid > 0)
    {
        GetUserPartition().Free(t->stack_block_uid);
        t->stack_block_uid = 0;
        t->stack_base = 0;
    }
}

void Scheduler::ReportFatalStopLocked(const std::string& reason)
{
    if (!fatal_stop_)
    {
        fatal_stop_ = true;
        fatal_stop_reason_ = reason;
        SetCrashStopReason(fatal_stop_reason_.c_str());
    }
    if (active_thread_uid_ != 0)
    {
        active_thread_uid_ = 0;
    }
    cv_.notify_all();
}

void Scheduler::AbortIfMutexHeldAfterGuardedRun(const platform::GuardedRunResult& res)
{
    if (!mutex_.UnlockIfHeldByCurrentThread())
    {
        return;
    }
    // Freeze who held the lock: once released, another scheduler thread may
    // take it before the crash report is written.
    SetCrashSchedulerMutexRecovery(platform::GetCurrentHostThreadId(), t_current_thread_uid);

    std::string orig_reason;
    if (!res.succeeded)
    {
        orig_reason = res.stopped ? res.stop_reason : ("Guest hardware fault: " + res.description);
    }
    else
    {
        orig_reason = "GuardedRun returned while holding scheduler mutex";
    }
    const std::string fatal_reason = "Stop/fault while holding scheduler lock: " + orig_reason;

    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        ReportFatalStopLocked(fatal_reason);
    }
    SetCrashStopReason(fatal_reason.c_str());
    platform::WriteLogFileLine("FATAL", fatal_reason.c_str());
    std::fprintf(stderr, "[FATAL] %s\n", fatal_reason.c_str());
    std::fflush(stderr);
    std::abort();
}

[[noreturn]] void Scheduler::TriggerMutexHeldStopForTest()
{
    mutex_.lock();
    platform::RaiseStop("simulated stop while holding scheduler mutex");
}

void Scheduler::SwitchOutCurrentLocked(std::unique_lock<TrackedMutex>& lock, const std::shared_ptr<GuestThread>& cur)
{
    UpdateCurrentHostStackHighWaterLocked(cur);
    if (in_interrupt_)
    {
        ReportFatalStopLocked("Wait or yield called inside interrupt handler");
        lock.unlock();
        platform::RaiseStop("Wait or yield called inside interrupt handler");
    }

    DeliverPendingInterruptsLocked(lock);

    if (ops_ != nullptr && ops_->save != nullptr && !cur->cpu_context.empty())
    {
        ops_->save(cur->cpu_context.data());
    }

    active_thread_uid_ = 0;
    cv_.notify_all();

    cv_.wait(lock, [&]()
    {
        return shutdown_ || fatal_stop_ || cur->state == ThreadState::kDead || active_thread_uid_ == cur->uid;
    });

    if (shutdown_ || fatal_stop_ || cur->state == ThreadState::kDead)
    {
        // All outer frames (NidRegistry::Invoke, HLE wait wrappers) hold no mutex locks
        // across RaiseStop. Only local objects in an inner waiting handler frame may
        // be skipped if the thread is force-terminated while blocked.
        lock.unlock();
        platform::RaiseStop(kInternalThreadExitStop);
    }

    if (ops_ != nullptr && ops_->restore != nullptr && !cur->cpu_context.empty())
    {
        ops_->restore(cur->cpu_context.data());
    }
}

void Scheduler::WakeWaitersLocked(WaitType type, KernelUid target_uid, int32_t result, bool wake_all)
{
    if (type == WaitType::kHostWait)
    {
        return;
    }
    for (const auto& t : threads_)
    {
        if (t->state == ThreadState::kWaiting && t->wait_type == type && t->wait_target_uid == target_uid)
        {
            t->wait_result = result;
            MakeReadyLocked(t);
            if (!wake_all)
            {
                break;
            }
        }
    }
}

int32_t Scheduler::RunPendingCallbacksLocked(std::unique_lock<TrackedMutex>& lock, const std::shared_ptr<GuestThread>& cur)
{
    if (!cur || !intr_enabled_ || ops_ == nullptr || ops_->call_guest == nullptr)
    {
        return 0;
    }

    int32_t executed = 0;
    for (size_t i = 0; i < cur->callbacks.size();)
    {
        KernelUid cb_uid = cur->callbacks[i];
        std::shared_ptr<GuestCallback> cb;
        if (GetKernelObjects().LookupAs(cb_uid, KernelObjectType::kCallback, &cb) != LookupStatus::kOk || !cb)
        {
            cur->callbacks.erase(cur->callbacks.begin() + static_cast<ptrdiff_t>(i));
            continue;
        }
        if (cb->notify_count <= 0)
        {
            ++i;
            continue;
        }

        const int32_t count = cb->notify_count;
        const int32_t arg = cb->last_arg;
        cb->notify_count = 0;

        std::vector<uint8_t> saved_ctx(ops_->context_size);
        if (ops_->save != nullptr && !saved_ctx.empty())
        {
            ops_->save(saved_ctx.data());
        }

        const uint32_t entry = cb->entry;
        const uint32_t common = cb->common_arg;
        const uint32_t gp = cur->gp_value;

        lock.unlock();
        if (ops_->set_gpr != nullptr)
        {
            ops_->set_gpr(kRegA0, static_cast<uint32_t>(count));
            ops_->set_gpr(kRegA1, static_cast<uint32_t>(arg));
            ops_->set_gpr(kRegA2, common);
            ops_->set_gpr(kRegGP, gp);
            ops_->set_gpr(kRegRA, 0);
        }
        ops_->call_guest(entry);
        int32_t cb_ret = (ops_->get_gpr != nullptr) ? static_cast<int32_t>(ops_->get_gpr(kRegV0)) : 0;
        if (ops_->restore != nullptr && !saved_ctx.empty())
        {
            ops_->restore(saved_ctx.data());
        }
        lock.lock();

        ++executed;
        if (cb_ret != 0)
        {
            GetKernelObjects().Remove(cb_uid, KernelObjectType::kCallback);
            cur->callbacks.erase(cur->callbacks.begin() + static_cast<ptrdiff_t>(i));
        }
        else
        {
            ++i;
        }
    }
    return executed;
}

void Scheduler::ThreadEntry(std::shared_ptr<GuestThread> self)
{
    struct ResetCurrentThreadUid
    {
        ~ResetCurrentThreadUid()
        {
            ClearCurrentThreadExecInfo();
            t_current_thread_uid = 0;
        }
    } reset_uid_guard;

    {
        std::unique_lock<TrackedMutex> lock(mutex_);
        cv_.wait(lock, [&]()
        {
            return shutdown_ || fatal_stop_ || active_thread_uid_ == self->uid;
        });
        if (shutdown_ || fatal_stop_)
        {
            return;
        }
        t_current_thread_uid = self->uid;
        SetCurrentThreadExecInfo(
            platform::GetCurrentHostThreadId(),
            self->uid,
            self->name.c_str(),
            self->current_priority);
        UpdateCurrentHostStackHighWaterLocked(self);
        if (ops_ != nullptr && ops_->restore != nullptr && !self->cpu_context.empty())
        {
            ops_->restore(self->cpu_context.data());
        }
    }

    auto res = platform::GuardedRun([&]()
    {
        if (ops_ != nullptr && ops_->call_guest != nullptr)
        {
            ops_->call_guest(self->entry);
        }
    });
    AbortIfMutexHeldAfterGuardedRun(res);

    std::unique_lock<TrackedMutex> lock(mutex_);
    UpdateCurrentHostStackHighWaterLocked(self);
    if (!res.succeeded)
    {
        if (res.stopped && res.stop_reason == kInternalThreadExitStop)
        {
            // Clean thread termination via ExitThread / shutdown.
            return;
        }
        std::string reason = res.stopped ? res.stop_reason : ("Guest hardware fault: " + res.description);
        self->state = ThreadState::kDead;
        ReportFatalStopLocked(reason);
        return;
    }

    int32_t exit_code = 0;
    if (ops_ != nullptr && ops_->get_gpr != nullptr)
    {
        exit_code = static_cast<int32_t>(ops_->get_gpr(kRegV0));
    }

    self->exit_status = exit_code;
    self->state = ThreadState::kDead;
    WakeWaitersLocked(WaitType::kThreadEnd, self->uid, kWaitOk, true);
    if (self->delete_on_exit)
    {
        FreeThreadStackLocked(self);
    }
    if (active_thread_uid_ == self->uid)
    {
        active_thread_uid_ = 0;
    }
    cv_.notify_all();
}

int32_t Scheduler::CreateThread(const std::string& name, uint32_t entry, int32_t init_priority, uint32_t stack_size, uint32_t attr, uint32_t gp_value)
{
    if (init_priority < 0x08 || init_priority > 0x77)
    {
        // Note: module_start uses 0x20; allow standard PSP user priority range [8..119].
        if (init_priority <= 0 || init_priority > 0x7F)
        {
            return kErrIllegalPrio;
        }
    }
    if (stack_size < 0x200)
    {
        return kErrIllegalStackSize;
    }

    const uint32_t aligned_stack = AlignUp(stack_size, 256u);
    uint32_t stack_addr = 0;
    int32_t block_uid = GetUserPartition().Alloc(MemAllocType::kHigh, aligned_stack, 0, "stack:" + name, &stack_addr);
    if (block_uid < 0)
    {
        return kErrNoMemory;
    }

    // Fill thread stack with 0xFF by default, or 0x00 with CLEAR_STACK (uOFW threadman).
    constexpr uint32_t kAttrNoFillStack = 0x00100000u;
    constexpr uint32_t kAttrClearStack = 0x00400000u;
    if ((attr & kAttrNoFillStack) == 0)
    {
        if (uint8_t* stack_ptr = GetGuestMemory().Translate(stack_addr))
        {
            const uint8_t fill_byte = ((attr & kAttrClearStack) != 0) ? 0x00u : 0xFFu;
            std::memset(stack_ptr, fill_byte, aligned_stack);
        }
    }

    auto t = std::make_shared<GuestThread>();
    t->name = name;
    t->entry = entry;
    t->init_priority = init_priority;
    t->current_priority = init_priority;
    t->stack_size = aligned_stack;
    t->attr = attr;
    t->gp_value = gp_value;
    t->stack_block_uid = block_uid;
    t->stack_base = stack_addr;
    t->initial_sp = (stack_addr + aligned_stack - 64u) & ~15u;
    t->state = ThreadState::kDormant;

    std::lock_guard<TrackedMutex> lock(mutex_);
    if (ops_ != nullptr && ops_->context_size > 0)
    {
        t->cpu_context.resize(ops_->context_size, 0);
    }
    t->uid = GetKernelObjects().Create(KernelObjectType::kThread, t);
    threads_.push_back(t);
    return t->uid;
}

int32_t Scheduler::StartThread(KernelUid thread_uid, uint32_t arglen, uint32_t argp)
{
    std::shared_ptr<GuestThread> t;
    std::shared_ptr<GuestThread> cur;
    bool should_yield = false;
    {
        std::unique_lock<TrackedMutex> lock(mutex_);
        t = FindThreadLocked(thread_uid);
        if (!t)
        {
            return kErrUnknownThid;
        }
        if (t->state != ThreadState::kDormant)
        {
            return kErrNotDormant;
        }

        // Place optional argument block and 256-byte TLS ($k0) block at top of guest stack (uOFW convention).
        const uint32_t stack_top = t->stack_base + t->stack_size;
        uint32_t copied_arg_addr = 0;
        uint32_t top_cursor = stack_top;
        if (arglen > 0 && argp != 0)
        {
            const uint32_t aligned_arg = AlignUp(arglen, 16u);
            if (stack_top - aligned_arg >= t->stack_base + 0x140u)
            {
                copied_arg_addr = stack_top - aligned_arg;
                const uint8_t* src = GetGuestMemory().Translate(argp);
                uint8_t* dst = GetGuestMemory().Translate(copied_arg_addr);
                std::memcpy(dst, src, arglen);
                top_cursor = copied_arg_addr;
            }
        }
        const uint32_t k0_addr = (top_cursor - 0x100u) & ~15u;
        if (uint8_t* k0_ptr = GetGuestMemory().Translate(k0_addr))
        {
            std::memset(k0_ptr, 0, 0x100u);
        }
        const uint32_t sp = (k0_addr - 64u) & ~15u;
        t->initial_sp = sp;

        // Initialize guest thread CPU context using ops save/restore into a temporary slot.
        if (ops_ != nullptr && ops_->context_size > 0)
        {
            if (t->cpu_context.size() != ops_->context_size)
            {
                t->cpu_context.assign(ops_->context_size, 0);
            }
            std::vector<uint8_t> saved_cur(ops_->context_size, 0);
            if (active_thread_uid_ != 0 && ops_->save != nullptr)
            {
                ops_->save(saved_cur.data());
            }
            if (ops_->restore != nullptr)
            {
                ops_->restore(t->cpu_context.data());
            }
            if (ops_->set_gpr != nullptr)
            {
                ops_->set_gpr(kRegA0, (copied_arg_addr != 0) ? arglen : 0);
                ops_->set_gpr(kRegA1, copied_arg_addr);
                ops_->set_gpr(kRegK0, k0_addr);
                ops_->set_gpr(kRegGP, t->gp_value);
                ops_->set_gpr(kRegSP, t->initial_sp);
                ops_->set_gpr(kRegRA, 0);
            }
            if (ops_->save != nullptr)
            {
                ops_->save(t->cpu_context.data());
            }
            if (active_thread_uid_ != 0 && ops_->restore != nullptr)
            {
                ops_->restore(saved_cur.data());
            }
        }

        t->current_priority = t->init_priority;
        t->exit_status = 0;
        t->wakeup_count = 0;
        MakeReadyLocked(t);

        if (!t->spawned)
        {
            t->spawned = true;
            t->host_stack_reserve_bytes = guest_host_stack_reserve_bytes_;
            platform::HostThreadOptions thread_opts{};
            thread_opts.stack_reserve_bytes = guest_host_stack_reserve_bytes_;
            t->host_thread = std::make_unique<platform::HostThread>(
                "guest:" + t->name,
                [this, t]()
                {
                    ThreadEntry(t);
                },
                thread_opts);
        }

        cur = FindThreadLocked(active_thread_uid_);
        if (cur && intr_enabled_ && t->current_priority < cur->current_priority)
        {
            should_yield = true;
        }
        cv_.notify_all();
    }

    if (should_yield)
    {
        YieldIfPreempted();
    }
    return 0;
}

[[noreturn]] void Scheduler::ExitThread(int32_t status, bool delete_self)
{
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        auto cur = FindThreadLocked(active_thread_uid_);
        if (cur)
        {
            UpdateCurrentHostStackHighWaterLocked(cur);
            cur->exit_status = status;
            cur->state = ThreadState::kDead;
            cur->delete_on_exit = delete_self;
            WakeWaitersLocked(WaitType::kThreadEnd, cur->uid, kWaitOk, true);
            if (delete_self)
            {
                FreeThreadStackLocked(cur);
            }
            active_thread_uid_ = 0;
            cv_.notify_all();
        }
    }
    platform::RaiseStop(kInternalThreadExitStop);
}

int32_t Scheduler::DeleteThread(KernelUid thread_uid)
{
    std::shared_ptr<GuestThread> target;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        target = FindThreadLocked(thread_uid);
        if (!target)
        {
            return kErrUnknownThid;
        }
        if (target->uid == active_thread_uid_ ||
            (target->state == ThreadState::kWaiting && target->wait_type == WaitType::kHostWait))
        {
            return kErrIllegalContext;
        }
        if (target->state != ThreadState::kDormant && target->state != ThreadState::kDead)
        {
            return kErrNotDormant;
        }
        RemoveFromReadyQueueLocked(target->uid);
        FreeThreadStackLocked(target);
        GetKernelObjects().Remove(target->uid, KernelObjectType::kThread);
        threads_.erase(std::remove(threads_.begin(), threads_.end(), target), threads_.end());
    }
    if (target && target->host_thread)
    {
        target->host_thread->Join();
        target->host_thread.reset();
    }
    return 0;
}

int32_t Scheduler::TerminateThread(KernelUid thread_uid, bool delete_target)
{
    std::shared_ptr<GuestThread> target;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        target = FindThreadLocked(thread_uid);
        if (!target)
        {
            return kErrUnknownThid;
        }
        if (target->uid == active_thread_uid_ ||
            (target->state == ThreadState::kWaiting && target->wait_type == WaitType::kHostWait))
        {
            return kErrIllegalContext;
        }
        RemoveFromReadyQueueLocked(target->uid);
        if (target->has_timeout)
        {
            target->has_timeout = false;
            if (timed_waiter_count_ > 0)
            {
                --timed_waiter_count_;
            }
        }
        target->state = ThreadState::kDead;
        target->exit_status = static_cast<int32_t>(0x800201ACu);
        WakeWaitersLocked(WaitType::kThreadEnd, target->uid, kWaitOk, true);
        if (delete_target)
        {
            FreeThreadStackLocked(target);
            GetKernelObjects().Remove(target->uid, KernelObjectType::kThread);
            threads_.erase(std::remove(threads_.begin(), threads_.end(), target), threads_.end());
        }
        cv_.notify_all();
    }
    if (target && target->host_thread && target->state == ThreadState::kDead)
    {
        // Note: host_thread will exit on wakeup if it was waiting.
        target->host_thread->Join();
        target->host_thread.reset();
    }
    return 0;
}

int32_t Scheduler::WaitThreadEnd(KernelUid thread_uid, uint32_t* timeout_us, bool allow_callbacks)
{
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        auto target = FindThreadLocked(thread_uid);
        if (!target)
        {
            return kErrUnknownThid;
        }
        if (target->state == ThreadState::kDormant || target->state == ThreadState::kDead)
        {
            return 0;
        }
    }
    return WaitCurrent(WaitType::kThreadEnd, thread_uid, "WaitThreadEnd", timeout_us, allow_callbacks);
}

KernelUid Scheduler::GetCurrentThreadId() const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    return active_thread_uid_;
}

int32_t Scheduler::GetThreadPriority(KernelUid thread_uid) const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    KernelUid target_uid = (thread_uid == 0) ? active_thread_uid_ : thread_uid;
    auto target = FindThreadLocked(target_uid);
    if (!target)
    {
        return kErrUnknownThid;
    }
    return target->current_priority;
}

int32_t Scheduler::ChangeThreadPriority(KernelUid thread_uid, int32_t new_priority)
{
    if (new_priority <= 0 || new_priority > 0x7F)
    {
        return kErrIllegalPrio;
    }
    bool should_yield = false;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        KernelUid target_uid = (thread_uid == 0) ? active_thread_uid_ : thread_uid;
        auto target = FindThreadLocked(target_uid);
        if (!target)
        {
            return kErrUnknownThid;
        }
        if (target->state == ThreadState::kDormant)
        {
            return kErrThreadDormant;
        }
        target->current_priority = new_priority;
        auto cur = FindThreadLocked(active_thread_uid_);
        if (cur && intr_enabled_ && HasHigherPriorityReadyLocked(cur->current_priority))
        {
            should_yield = true;
        }
    }
    if (should_yield)
    {
        YieldIfPreempted();
    }
    return 0;
}

int32_t Scheduler::SleepThread(bool allow_callbacks)
{
    {
        std::unique_lock<TrackedMutex> lock(mutex_);
        RaiseStopIfShuttingDownLocked(shutdown_ || fatal_stop_, lock);
        auto cur = FindThreadLocked(active_thread_uid_);
        if (!cur)
        {
            return 0;
        }
        if (cur->wakeup_count > 0)
        {
            --cur->wakeup_count;
            if (allow_callbacks)
            {
                RunPendingCallbacksLocked(lock, cur);
            }
            return 0;
        }
    }
    return WaitCurrent(WaitType::kSleep, 0, "SleepThread", nullptr, allow_callbacks);
}

int32_t Scheduler::WakeupThread(KernelUid thread_uid)
{
    bool should_yield = false;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        auto target = FindThreadLocked(thread_uid);
        if (!target)
        {
            return kErrUnknownThid;
        }
        if (target->state == ThreadState::kDormant)
        {
            return kErrThreadDormant;
        }
        if (target->state == ThreadState::kWaiting && target->wait_type == WaitType::kSleep)
        {
            target->wait_result = kWaitOk;
            MakeReadyLocked(target);
            auto cur = FindThreadLocked(active_thread_uid_);
            if (cur && intr_enabled_ && target->current_priority < cur->current_priority)
            {
                should_yield = true;
            }
            cv_.notify_all();
        }
        else
        {
            ++target->wakeup_count;
        }
    }
    if (should_yield)
    {
        YieldIfPreempted();
    }
    return 0;
}

int32_t Scheduler::DelayThread(uint32_t delay_us, bool allow_callbacks)
{
    if (delay_us == 0)
    {
        if (allow_callbacks)
        {
            CheckCallbacks();
        }
        YieldCurrent();
        return 0;
    }
    int32_t res = WaitCurrent(WaitType::kDelay, 0, "DelayThread", &delay_us, allow_callbacks);
    return (res == kWaitTimeout) ? 0 : res;
}

int32_t Scheduler::WaitCurrent(WaitType type, KernelUid target_uid, const std::string& detail, const uint32_t* timeout_us, bool allow_callbacks)
{
    return WaitCurrentIf(type, target_uid, detail, timeout_us, allow_callbacks, {});
}

int32_t Scheduler::WaitCurrentIf(
    WaitType type,
    KernelUid target_uid,
    const std::string& detail,
    const uint32_t* timeout_us,
    bool allow_callbacks,
    const std::function<bool()>& should_wait)
{
    std::unique_lock<TrackedMutex> lock(mutex_);
    RaiseStopIfShuttingDownLocked(shutdown_ || fatal_stop_, lock);
    if (in_interrupt_)
    {
        ReportFatalStopLocked("Wait called inside interrupt handler");
        lock.unlock();
        platform::RaiseStop("Wait called inside interrupt handler");
    }
    auto cur = FindThreadLocked(active_thread_uid_);
    if (!cur)
    {
        return 0;
    }

    if (allow_callbacks)
    {
        RunPendingCallbacksLocked(lock, cur);
    }

    if (should_wait && !should_wait())
    {
        return kWaitOk;
    }

    cur->state = ThreadState::kWaiting;
    cur->wait_type = type;
    cur->wait_target_uid = target_uid;
    cur->wait_detail = detail;
    cur->wait_result = kWaitOk;
    cur->wait_allow_callbacks = allow_callbacks;
    if (timeout_us != nullptr)
    {
        if (!cur->has_timeout)
        {
            cur->has_timeout = true;
            ++timed_waiter_count_;
        }
        cur->wait_start = std::chrono::steady_clock::now();
        cur->deadline = cur->wait_start + std::chrono::microseconds(*timeout_us);
    }
    else if (cur->has_timeout)
    {
        cur->has_timeout = false;
        if (timed_waiter_count_ > 0)
        {
            --timed_waiter_count_;
        }
    }

    SwitchOutCurrentLocked(lock, cur);

    if (allow_callbacks)
    {
        RunPendingCallbacksLocked(lock, cur);
    }

    return cur->wait_result;
}

bool Scheduler::WakeOneWaiter(WaitType type, KernelUid target_uid, int32_t result)
{
    if (type == WaitType::kHostWait)
    {
        return false;
    }
    bool woke = false;
    bool should_yield = false;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        for (const auto& t : threads_)
        {
            if (t->state == ThreadState::kWaiting && t->wait_type == type && t->wait_target_uid == target_uid)
            {
                t->wait_result = result;
                MakeReadyLocked(t);
                woke = true;
                auto cur = FindThreadLocked(active_thread_uid_);
                if (cur && cur->uid == t_current_thread_uid && intr_enabled_ && t->current_priority < cur->current_priority)
                {
                    should_yield = true;
                }
                break;
            }
        }
        if (woke)
        {
            cv_.notify_all();
        }
    }
    if (should_yield)
    {
        YieldIfPreempted();
    }
    return woke;
}

size_t Scheduler::WakeAllWaiters(WaitType type, KernelUid target_uid, int32_t result)
{
    if (type == WaitType::kHostWait)
    {
        return 0;
    }
    size_t count = 0;
    bool should_yield = false;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        auto cur = FindThreadLocked(active_thread_uid_);
        for (const auto& t : threads_)
        {
            if (t->state == ThreadState::kWaiting && t->wait_type == type && t->wait_target_uid == target_uid)
            {
                t->wait_result = result;
                MakeReadyLocked(t);
                ++count;
                if (cur && cur->uid == t_current_thread_uid && intr_enabled_ && t->current_priority < cur->current_priority)
                {
                    should_yield = true;
                }
            }
        }
        if (count > 0)
        {
            cv_.notify_all();
        }
    }
    if (should_yield)
    {
        YieldIfPreempted();
    }
    return count;
}

bool Scheduler::WakeThreadFromWait(KernelUid thread_uid, int32_t result)
{
    bool woke = false;
    bool should_yield = false;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        auto t = FindThreadLocked(thread_uid);
        if (t && t->state == ThreadState::kWaiting && t->wait_type != WaitType::kHostWait)
        {
            t->wait_result = result;
            MakeReadyLocked(t);
            woke = true;
            auto cur = FindThreadLocked(active_thread_uid_);
            if (cur && cur->uid == t_current_thread_uid && intr_enabled_ && t->current_priority < cur->current_priority)
            {
                should_yield = true;
            }
            cv_.notify_all();
        }
    }
    if (should_yield)
    {
        YieldIfPreempted();
    }
    return woke;
}

size_t Scheduler::WakeThreadsFromWait(const std::vector<KernelUid>& thread_uids, int32_t result)
{
    if (thread_uids.empty())
    {
        return 0;
    }
    size_t count = 0;
    bool should_yield = false;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        auto cur = FindThreadLocked(active_thread_uid_);
        for (KernelUid uid : thread_uids)
        {
            auto t = FindThreadLocked(uid);
            if (t && t->state == ThreadState::kWaiting)
            {
                t->wait_result = result;
                MakeReadyLocked(t);
                ++count;
                if (cur && cur->uid == t_current_thread_uid && intr_enabled_ && t->current_priority < cur->current_priority)
                {
                    should_yield = true;
                }
            }
        }
        if (count > 0)
        {
            cv_.notify_all();
        }
    }
    if (should_yield)
    {
        YieldIfPreempted();
    }
    return count;
}

int32_t Scheduler::CreateCallback(const std::string& name, uint32_t entry, uint32_t common_arg)
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    auto cur = FindThreadLocked(active_thread_uid_);
    auto cb = std::make_shared<GuestCallback>();
    cb->name = name;
    cb->entry = entry;
    cb->common_arg = common_arg;
    cb->owner_thread_uid = cur ? cur->uid : 0;
    cb->uid = GetKernelObjects().Create(KernelObjectType::kCallback, cb);
    if (cur)
    {
        cur->callbacks.push_back(cb->uid);
    }
    return cb->uid;
}

int32_t Scheduler::NotifyCallback(KernelUid cb_uid, int32_t arg)
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    std::shared_ptr<GuestCallback> cb;
    if (GetKernelObjects().LookupAs(cb_uid, KernelObjectType::kCallback, &cb) != LookupStatus::kOk || !cb)
    {
        return kErrUnknownCbid;
    }
    ++cb->notify_count;
    cb->last_arg = arg;
    return 0;
}

int32_t Scheduler::CheckCallbacks()
{
    std::unique_lock<TrackedMutex> lock(mutex_);
    RaiseStopIfShuttingDownLocked(shutdown_ || fatal_stop_, lock);
    auto cur = FindThreadLocked(active_thread_uid_);
    if (!cur)
    {
        return 0;
    }
    return RunPendingCallbacksLocked(lock, cur);
}

int32_t Scheduler::CallGuestFunction(uint32_t entry, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (entry == 0 || ops_ == nullptr || ops_->call_guest == nullptr)
    {
        return 0;
    }

    std::vector<uint8_t> saved_ctx;
    if (ops_->save != nullptr && ops_->context_size > 0)
    {
        saved_ctx.resize(ops_->context_size);
        ops_->save(saved_ctx.data());
    }

    const uint32_t gp = GetCurrentThreadGp();
    if (ops_->set_gpr != nullptr)
    {
        ops_->set_gpr(kRegA0, a0);
        ops_->set_gpr(kRegA1, a1);
        ops_->set_gpr(kRegA2, a2);
        ops_->set_gpr(kRegA3, a3);
        if (gp != 0)
        {
            ops_->set_gpr(kRegGP, gp);
        }
        ops_->set_gpr(kRegRA, 0);
    }

    ops_->call_guest(entry);

    const int32_t ret = (ops_->get_gpr != nullptr) ? static_cast<int32_t>(ops_->get_gpr(kRegV0)) : 0;
    if (ops_->restore != nullptr && !saved_ctx.empty())
    {
        ops_->restore(saved_ctx.data());
    }
    return ret;
}

uint32_t Scheduler::SuspendInterrupts()
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    uint32_t prev = intr_enabled_ ? 1u : 0u;
    intr_enabled_ = false;
    return prev;
}

void Scheduler::ResumeInterrupts(uint32_t prev_state)
{
    bool should_yield = false;
    bool should_deliver = false;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        intr_enabled_ = (prev_state != 0);
        if (intr_enabled_)
        {
            should_deliver = !pending_interrupts_.empty() && !in_interrupt_;
            auto cur = FindThreadLocked(active_thread_uid_);
            if (cur && HasHigherPriorityReadyLocked(cur->current_priority))
            {
                should_yield = true;
            }
        }
    }
    if (should_deliver)
    {
        DeliverPendingInterrupts();
    }
    if (should_yield)
    {
        YieldIfPreempted();
    }
}

bool Scheduler::AreInterruptsEnabled() const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    return intr_enabled_;
}

void Scheduler::QueueInterrupt(uint32_t entry, uint32_t arg0, uint32_t arg1, uint32_t gp_value)
{
    if (entry == 0)
    {
        return;
    }
    std::lock_guard<TrackedMutex> lock(mutex_);
    pending_interrupts_.push_back(PendingInterrupt{entry, arg0, arg1, gp_value});
    cv_.notify_all();
}

bool Scheduler::IsInInterrupt() const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    return in_interrupt_;
}

uint32_t Scheduler::GetCurrentThreadGp() const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    auto cur = FindThreadLocked(active_thread_uid_);
    if (cur && cur->gp_value != 0)
    {
        return cur->gp_value;
    }
    if (ops_ != nullptr && ops_->get_gpr != nullptr)
    {
        return ops_->get_gpr(kRegGP);
    }
    return 0;
}

void Scheduler::DeliverPendingInterruptsLocked(std::unique_lock<TrackedMutex>& lock)
{
    if (!intr_enabled_ || in_interrupt_ || pending_interrupts_.empty() || shutdown_ || fatal_stop_)
    {
        return;
    }
    if (ops_ == nullptr || ops_->call_guest == nullptr)
    {
        return;
    }

    std::vector<uint8_t> saved_ctx;
    if (ops_->save != nullptr && ops_->context_size > 0)
    {
        saved_ctx.resize(ops_->context_size);
        ops_->save(saved_ctx.data());
    }

    uint32_t fallback_gp = 0;
    auto cur = FindThreadLocked(active_thread_uid_);
    if (cur && cur->gp_value != 0)
    {
        fallback_gp = cur->gp_value;
    }
    else if (ops_->get_gpr != nullptr)
    {
        fallback_gp = ops_->get_gpr(kRegGP);
    }

    const bool idle_context = (active_thread_uid_ == 0);
    in_interrupt_ = true;

    while (!pending_interrupts_.empty() && intr_enabled_ && !shutdown_ && !fatal_stop_)
    {
        PendingInterrupt intr = pending_interrupts_.front();
        pending_interrupts_.erase(pending_interrupts_.begin());

        lock.unlock();

        if (ops_->set_gpr != nullptr)
        {
            ops_->set_gpr(kRegSP, kInterruptStackTop);
            ops_->set_gpr(kRegGP, intr.gp_value != 0 ? intr.gp_value : fallback_gp);
            ops_->set_gpr(kRegA0, intr.arg0);
            ops_->set_gpr(kRegA1, intr.arg1);
            ops_->set_gpr(kRegRA, 0);
        }

        if (idle_context)
        {
            auto guard = platform::GuardedRun([&]()
            {
                ops_->call_guest(intr.entry);
            });
            AbortIfMutexHeldAfterGuardedRun(guard);
            lock.lock();
            if (!guard.succeeded && guard.stopped)
            {
                ReportFatalStopLocked(guard.stop_reason);
                break;
            }
        }
        else
        {
            ops_->call_guest(intr.entry);
            lock.lock();
        }
    }

    in_interrupt_ = false;
    if (ops_->restore != nullptr && !saved_ctx.empty())
    {
        ops_->restore(saved_ctx.data());
    }
}

void Scheduler::DeliverPendingInterrupts()
{
    std::unique_lock<TrackedMutex> lock(mutex_);
    DeliverPendingInterruptsLocked(lock);
}

void Scheduler::YieldIfPreempted()
{
    std::unique_lock<TrackedMutex> lock(mutex_);
    RaiseStopIfShuttingDownLocked(shutdown_ || fatal_stop_, lock);
    if (!intr_enabled_ || in_interrupt_ || active_thread_uid_ == 0 || t_current_thread_uid == 0)
    {
        return;
    }
    auto cur = FindThreadLocked(active_thread_uid_);
    if (!cur || cur->uid != t_current_thread_uid || !HasHigherPriorityReadyLocked(cur->current_priority))
    {
        return;
    }
    // Preempted thread remains at the front of its priority ready queue (uITRON 4.0 scheduling rule).
    MakeReadyLocked(cur, true);
    SwitchOutCurrentLocked(lock, cur);
}

void Scheduler::YieldCurrent()
{
    std::unique_lock<TrackedMutex> lock(mutex_);
    RaiseStopIfShuttingDownLocked(shutdown_ || fatal_stop_, lock);
    if (in_interrupt_)
    {
        ReportFatalStopLocked("Yield called inside interrupt handler");
        lock.unlock();
        platform::RaiseStop("Yield called inside interrupt handler");
    }
    if (!intr_enabled_ || active_thread_uid_ == 0 || ready_queue_.empty())
    {
        return;
    }
    auto cur = FindThreadLocked(active_thread_uid_);
    if (!cur)
    {
        return;
    }
    // Only yield if another ready thread has <= priority (FIFO within same priority).
    bool has_peer = false;
    for (KernelUid uid : ready_queue_)
    {
        auto t = FindThreadLocked(uid);
        if (t && t->state == ThreadState::kReady && t->current_priority <= cur->current_priority)
        {
            has_peer = true;
            break;
        }
    }
    if (!has_peer)
    {
        return;
    }
    MakeReadyLocked(cur);
    SwitchOutCurrentLocked(lock, cur);
}

size_t Scheduler::ExpireTimedWaitsLocked(std::chrono::steady_clock::time_point now)
{
    if (timed_waiter_count_ == 0)
    {
        return 0;
    }
    size_t expired = 0;
    for (const auto& t : threads_)
    {
        if (t->state == ThreadState::kWaiting &&
            t->wait_type != WaitType::kHostWait &&
            t->has_timeout &&
            now >= t->deadline)
        {
            t->wait_result = kWaitTimeout;
            MakeReadyLocked(t);
            ++expired;
        }
    }
    return expired;
}

void Scheduler::Poll()
{
    DeliverPendingInterrupts();
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        if (timed_waiter_count_ > 0)
        {
            ExpireTimedWaitsLocked(std::chrono::steady_clock::now());
        }
    }
    YieldIfPreempted();
}

void Scheduler::SetExternalWaitSources(uint32_t count)
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    external_wait_sources_ = count;
    cv_.notify_all();
}

void Scheduler::AddExternalWaitSource(uint32_t count)
{
    if (count == 0)
    {
        return;
    }
    std::lock_guard<TrackedMutex> lock(mutex_);
    external_wait_sources_ += count;
    cv_.notify_all();
}

void Scheduler::RemoveExternalWaitSource(uint32_t count)
{
    if (count == 0)
    {
        return;
    }
    std::lock_guard<TrackedMutex> lock(mutex_);
    external_wait_sources_ = (external_wait_sources_ > count) ? (external_wait_sources_ - count) : 0u;
    cv_.notify_all();
}

void Scheduler::NotifyExternalEvent()
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    cv_.notify_all();
}

void Scheduler::RequestStop(const std::string& reason)
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    ReportFatalStopLocked(reason);
}

std::string Scheduler::FormatDeadlockReportLocked() const
{
    std::ostringstream oss;
    oss << "Deadlock: no ready threads and no pending timeouts";
    for (const auto& t : threads_)
    {
        oss << " | [uid=" << t->uid << " name=\"" << t->name << "\" prio=" << t->current_priority
            << " state=" << ThreadStateName(t->state);
        if (t->state == ThreadState::kWaiting)
        {
            oss << " wait=" << WaitTypeName(t->wait_type);
            if (t->wait_target_uid != 0)
            {
                oss << "(0x" << std::hex << t->wait_target_uid << std::dec << ")";
            }
            if (!t->wait_detail.empty())
            {
                oss << " detail=" << t->wait_detail;
            }
        }
        oss << "]";
    }
    return oss.str();
}

void Scheduler::Run(KernelUid entry_thread_uid)
{
    {
        std::unique_lock<TrackedMutex> lock(mutex_);
        auto entry = FindThreadLocked(entry_thread_uid);
        if (!entry)
        {
            lock.unlock();
            platform::RaiseStop("Scheduler::Run called with unknown entry thread UID");
        }
        running_ = true;
        shutdown_ = false;
        fatal_stop_ = false;
        fatal_stop_reason_.clear();
    }

    // Start entry thread if still dormant.
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        auto entry = FindThreadLocked(entry_thread_uid);
        if (entry && entry->state == ThreadState::kDormant)
        {
            // Unlock to call StartThread cleanly.
        }
    }
    StartThread(entry_thread_uid, 0, 0);

    bool stop_on_fatal = false;
    std::string stop_reason;

    {
        std::unique_lock<TrackedMutex> lock(mutex_);
        while (running_ && !fatal_stop_)
        {
            if (active_thread_uid_ != 0)
            {
                cv_.wait(lock, [&]()
                {
                    return !running_ || fatal_stop_ || active_thread_uid_ == 0;
                });
                continue;
            }

            auto next = PickNextReadyLocked();
            if (next)
            {
                if (wake_latency_enabled_ && next->has_wake_stamp)
                {
                    const auto now = std::chrono::steady_clock::now();
                    const uint64_t us = (now > next->wake_time)
                        ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now - next->wake_time).count())
                        : 0u;
                    const size_t bucket = WakeLatencyBucketIndex(us);
                    const size_t wt_idx = static_cast<size_t>(next->wake_wait_type);
                    const size_t prio_idx = PriorityBucketIndex(next->current_priority);
                    ++wake_stats_.total_wakes;
                    wake_stats_.total_latency_us += us;
                    if (us > wake_stats_.max_latency_us)
                    {
                        wake_stats_.max_latency_us = us;
                    }
                    ++wake_stats_.hist[bucket];
                    if (wt_idx < wake_stats_.by_wait_type.size())
                    {
                        RecordWakeBucket(wake_stats_.by_wait_type[wt_idx], bucket, us);
                    }
                    RecordWakeBucket(wake_stats_.by_priority[prio_idx], bucket, us);
                    next->has_wake_stamp = false;
                }
                next->state = ThreadState::kRunning;
                active_thread_uid_ = next->uid;
                cv_.notify_all();
                cv_.wait(lock, [&]()
                {
                    return !running_ || fatal_stop_ || active_thread_uid_ == 0;
                });
                continue;
            }

            if (intr_enabled_ && !pending_interrupts_.empty())
            {
                DeliverPendingInterruptsLocked(lock);
                continue;
            }

            // No ready threads; check if any thread is still alive (waiting or suspended).
            bool any_alive = false;
            bool has_timeout = false;
            std::chrono::steady_clock::time_point min_deadline;

            for (const auto& t : threads_)
            {
                if (t->state == ThreadState::kWaiting || t->state == ThreadState::kSuspended)
                {
                    any_alive = true;
                    if (t->state == ThreadState::kWaiting && t->wait_type != WaitType::kHostWait && t->has_timeout)
                    {
                        if (!has_timeout || t->deadline < min_deadline)
                        {
                            min_deadline = t->deadline;
                            has_timeout = true;
                        }
                    }
                }
            }

            if (!any_alive)
            {
                // All started threads exited normally.
                break;
            }

            if (!has_timeout && external_wait_sources_ == 0)
            {
                fatal_stop_ = true;
                fatal_stop_reason_ = FormatDeadlockReportLocked();
                break;
            }

            if (has_timeout)
            {
                cv_.wait_until(lock, min_deadline);
                ExpireTimedWaitsLocked(std::chrono::steady_clock::now());
            }
            else
            {
                cv_.wait(lock);
            }
        }

        stop_on_fatal = fatal_stop_;
        stop_reason = fatal_stop_reason_;
        running_ = false;
        shutdown_ = true;
        cv_.notify_all();
    }

    // Join all host threads cleanly before propagating any RaiseStop to caller's GuardedRun.
    std::vector<std::shared_ptr<GuestThread>> all_threads;
    {
        std::lock_guard<TrackedMutex> lock(mutex_);
        all_threads = threads_;
    }
    for (auto& t : all_threads)
    {
        if (t->host_thread)
        {
            t->host_thread->Join();
            t->host_thread.reset();
        }
    }

    if (stop_on_fatal)
    {
        platform::RaiseStop(stop_reason.c_str());
    }
}

void Scheduler::SetWakeLatencyProfiling(bool enabled)
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    wake_latency_enabled_ = enabled;
    wake_stats_.enabled = enabled;
}

WakeLatencyStats Scheduler::GetWakeLatencyStats() const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    return wake_stats_;
}

std::string Scheduler::FormatWakeLatencyStats() const
{
    const WakeLatencyStats s = GetWakeLatencyStats();
    if (!s.enabled)
    {
        return "wake_latency=disabled";
    }
    std::ostringstream oss;
    const uint64_t avg_us = (s.total_wakes > 0) ? (s.total_latency_us / s.total_wakes) : 0u;
    oss << "wakes=" << s.total_wakes
        << " avg_us=" << avg_us
        << " max_us=" << s.max_latency_us
        << " hist(<0.1/0.5/1/2/5/10/20/50/>=50ms)="
        << s.hist[0] << "/" << s.hist[1] << "/" << s.hist[2] << "/"
        << s.hist[3] << "/" << s.hist[4] << "/" << s.hist[5] << "/"
        << s.hist[6] << "/" << s.hist[7] << "/" << s.hist[8];
    for (size_t i = 0; i < s.by_wait_type.size(); ++i)
    {
        const auto& b = s.by_wait_type[i];
        if (b.count > 0)
        {
            oss << " | " << WaitTypeName(static_cast<WaitType>(i))
                << "(n=" << b.count
                << ",avg=" << (b.total_us / b.count) << "us"
                << ",max=" << b.max_us << "us"
                << ",>=10ms=" << (b.hist[6] + b.hist[7] + b.hist[8]) << ")";
        }
    }
    static constexpr const char* kPrioNames[4] = { "p0-15", "p16-31", "p32-63", "p64+" };
    for (size_t i = 0; i < s.by_priority.size(); ++i)
    {
        const auto& b = s.by_priority[i];
        if (b.count > 0)
        {
            oss << " | " << kPrioNames[i]
                << "(n=" << b.count
                << ",avg=" << (b.total_us / b.count) << "us"
                << ",max=" << b.max_us << "us"
                << ",>=10ms=" << (b.hist[6] + b.hist[7] + b.hist[8]) << ")";
        }
    }
    return oss.str();
}

std::string Scheduler::FormatHostStackStats() const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    std::ostringstream oss;
    oss << "=== Guest Host Stack High-Water ===\n";
    for (const auto& t : threads_)
    {
        oss << "  [uid=0x" << std::hex << t->uid << std::dec
            << "] name=\"" << t->name << "\""
            << " reserve=" << t->host_stack_reserve_bytes << " B"
            << " high_water=" << t->host_stack_high_water_bytes << " B"
            << " (" << (t->host_stack_high_water_bytes / 1024u) << " KiB)\n";
    }
    return oss.str();
}

std::vector<GuestThreadInfo> Scheduler::GetThreadsSnapshot() const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    std::vector<GuestThreadInfo> out;
    out.reserve(threads_.size());
    for (const auto& t : threads_)
    {
        GuestThreadInfo info;
        info.uid = t->uid;
        info.name = t->name;
        info.entry = t->entry;
        info.init_priority = t->init_priority;
        info.current_priority = t->current_priority;
        info.stack_size = t->stack_size;
        info.attr = t->attr;
        info.gp_value = t->gp_value;
        info.state = t->state;
        info.wait_type = t->wait_type;
        info.wait_target_uid = t->wait_target_uid;
        info.wait_detail = t->wait_detail;
        info.exit_status = t->exit_status;
        info.host_stack_reserve_bytes = t->host_stack_reserve_bytes;
        info.host_stack_high_water_bytes = t->host_stack_high_water_bytes;
        out.push_back(std::move(info));
    }
    return out;
}

bool Scheduler::GetThreadWaitDeadlineForTest(KernelUid thread_uid, bool* has_timeout, int64_t* duration_us) const
{
    std::lock_guard<TrackedMutex> lock(mutex_);
    for (const auto& t : threads_)
    {
        if (t->uid == thread_uid)
        {
            if (t->state != ThreadState::kWaiting)
            {
                return false;
            }
            if (has_timeout != nullptr)
            {
                *has_timeout = t->has_timeout;
            }
            if (duration_us != nullptr)
            {
                *duration_us = t->has_timeout
                                   ? std::chrono::duration_cast<std::chrono::microseconds>(t->deadline - t->wait_start)
                                         .count()
                                   : 0;
            }
            return true;
        }
    }
    return false;
}

Scheduler& GetScheduler()
{
    static Scheduler instance;
    return instance;
}

Scheduler::ScopedHostWait::ScopedHostWait(Scheduler& sched)
    : sched_(sched),
      active_(sched.BeginHostWait())
{
}

Scheduler::ScopedHostWait::~ScopedHostWait()
{
    if (active_)
    {
        sched_.EndHostWait();
    }
}

bool Scheduler::BeginHostWait()
{
    if (t_current_thread_uid == 0)
    {
        return false;
    }
    std::lock_guard<TrackedMutex> lock(mutex_);
    if (shutdown_ || fatal_stop_ || in_interrupt_ || active_thread_uid_ != t_current_thread_uid)
    {
        return false;
    }
    auto cur = FindThreadLocked(t_current_thread_uid);
    if (!cur || cur->state != ThreadState::kRunning)
    {
        return false;
    }
    if (ops_ != nullptr && ops_->save != nullptr && !cur->cpu_context.empty())
    {
        ops_->save(cur->cpu_context.data());
    }
    cur->state = ThreadState::kWaiting;
    cur->wait_type = WaitType::kHostWait;
    cur->wait_target_uid = 0;
    cur->wait_detail = "host_wait";
    cur->has_timeout = false;
    active_thread_uid_ = 0;
    ++external_wait_sources_;
    cv_.notify_all();
    return true;
}

void Scheduler::EndHostWait()
{
    std::unique_lock<TrackedMutex> lock(mutex_);
    if (external_wait_sources_ > 0)
    {
        --external_wait_sources_;
    }
    auto cur = FindThreadLocked(t_current_thread_uid);
    if (!cur)
    {
        return;
    }
    if (shutdown_ || fatal_stop_ || cur->state == ThreadState::kDead)
    {
        return;
    }
    MakeReadyLocked(cur);
    cv_.notify_all();
    cv_.wait(lock, [&]()
    {
        return shutdown_ || fatal_stop_ || cur->state == ThreadState::kDead ||
               (active_thread_uid_ == cur->uid && cur->state == ThreadState::kRunning);
    });
    if (!shutdown_ && !fatal_stop_ && cur->state == ThreadState::kRunning &&
        ops_ != nullptr && ops_->restore != nullptr && !cur->cpu_context.empty())
    {
        ops_->restore(cur->cpu_context.data());
    }
}

} // namespace repsp::runtime
