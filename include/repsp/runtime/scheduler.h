#ifndef REPSP_RUNTIME_SCHEDULER_H
#define REPSP_RUNTIME_SCHEDULER_H

#include "repsp/platform/host_thread.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/kernel_objects.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace repsp::runtime
{

enum class ThreadState
{
    kDormant = 0,
    kReady,
    kRunning,
    kWaiting,
    kSuspended,
    kDead,
};

enum class WaitType
{
    kNone = 0,
    kThreadEnd,
    kSleep,
    kDelay,
    kSema,
    kEventFlag,
    kAsyncIo,
    kUmd,
    kCustom,
};

struct GuestThreadInfo
{
    KernelUid uid = 0;
    std::string name;
    uint32_t entry = 0;
    int32_t init_priority = 0x20;
    int32_t current_priority = 0x20;
    uint32_t stack_size = 0;
    uint32_t attr = 0;
    uint32_t gp_value = 0;
    ThreadState state = ThreadState::kDormant;
    WaitType wait_type = WaitType::kNone;
    KernelUid wait_target_uid = 0;
    std::string wait_detail;
    int32_t exit_status = 0;
};

struct GuestCallback
{
    KernelUid uid = 0;
    std::string name;
    uint32_t entry = 0;
    uint32_t common_arg = 0;
    KernelUid owner_thread_uid = 0;
    int32_t notify_count = 0;
    int32_t last_arg = 0;
};

class Scheduler
{
public:
    static constexpr int32_t kWaitOk = 0;
    static constexpr int32_t kWaitTimeout = static_cast<int32_t>(0x800201A8u);   // SCE_KERNEL_ERROR_WAIT_TIMEOUT (uOFW)
    static constexpr int32_t kWaitCancelled = static_cast<int32_t>(0x800201A9u); // SCE_KERNEL_ERROR_WAIT_CANCEL (uOFW)
    static constexpr int32_t kWaitReleased = static_cast<int32_t>(0x800201AAu);  // SCE_KERNEL_ERROR_RELEASE_WAIT (uOFW)
    static constexpr int32_t kWaitDeleted = static_cast<int32_t>(0x800201B5u);   // SCE_KERNEL_ERROR_WAIT_DELETE (uOFW)
    static constexpr uint32_t kInterruptStackBase = 0x08010000u;
    static constexpr uint32_t kInterruptStackSize = 16u * 1024u;
    static constexpr uint32_t kInterruptStackTop = kInterruptStackBase + kInterruptStackSize - 16u;

    struct PendingInterrupt
    {
        uint32_t entry = 0;
        uint32_t arg0 = 0;
        uint32_t arg1 = 0;
        uint32_t gp_value = 0;
    };

    void Reset();

    void SetCpuOps(const repsp_guest_cpu_ops* ops);
    const repsp_guest_cpu_ops* GetCpuOps() const { return ops_; }

    // Thread lifecycle
    int32_t CreateThread(const std::string& name, uint32_t entry, int32_t init_priority, uint32_t stack_size, uint32_t attr, uint32_t gp_value);
    int32_t StartThread(KernelUid thread_uid, uint32_t arglen, uint32_t argp);
    [[noreturn]] void ExitThread(int32_t status, bool delete_self);
    int32_t DeleteThread(KernelUid thread_uid);
    int32_t TerminateThread(KernelUid thread_uid, bool delete_target);
    int32_t WaitThreadEnd(KernelUid thread_uid, uint32_t* timeout_us, bool allow_callbacks);
    KernelUid GetCurrentThreadId() const;
    int32_t GetThreadPriority(KernelUid thread_uid) const;
    int32_t ChangeThreadPriority(KernelUid thread_uid, int32_t new_priority);
    int32_t SleepThread(bool allow_callbacks);
    int32_t WakeupThread(KernelUid thread_uid);
    int32_t DelayThread(uint32_t delay_us, bool allow_callbacks);

    // Generic wait / wake primitives for synchronization objects and later HLE modules
    int32_t WaitCurrent(WaitType type, KernelUid target_uid, const std::string& detail, const uint32_t* timeout_us, bool allow_callbacks);
    bool WakeOneWaiter(WaitType type, KernelUid target_uid, int32_t result);
    size_t WakeAllWaiters(WaitType type, KernelUid target_uid, int32_t result);
    bool WakeThreadFromWait(KernelUid thread_uid, int32_t result);
    size_t WakeThreadsFromWait(const std::vector<KernelUid>& thread_uids, int32_t result);

    // Callbacks
    int32_t CreateCallback(const std::string& name, uint32_t entry, uint32_t common_arg);
    int32_t NotifyCallback(KernelUid cb_uid, int32_t arg);
    int32_t CheckCallbacks();

    // Interrupt state (CpuSuspendIntr / CpuResumeIntr) and interrupt queue
    uint32_t SuspendInterrupts();
    void ResumeInterrupts(uint32_t prev_state);
    bool AreInterruptsEnabled() const;
    void QueueInterrupt(uint32_t entry, uint32_t arg0, uint32_t arg1, uint32_t gp_value = 0);
    void DeliverPendingInterrupts();
    bool IsInInterrupt() const;
    uint32_t GetCurrentThreadGp() const;

    // Switch points
    void YieldIfPreempted();
    void YieldCurrent();
    void Poll();

    // External event notification (e.g. vblank or host event)
    void SetExternalWaitSources(uint32_t count);
    void NotifyExternalEvent();

    // Run scheduler loop starting with entry_thread_uid (blocks until all threads finish or RaiseStop).
    void Run(KernelUid entry_thread_uid);

    // Diagnostics
    std::vector<GuestThreadInfo> GetThreadsSnapshot() const;

private:
    struct GuestThread
    {
        KernelUid uid = 0;
        std::string name;
        uint32_t entry = 0;
        int32_t init_priority = 0x20;
        int32_t current_priority = 0x20;
        uint32_t stack_size = 0;
        uint32_t attr = 0;
        uint32_t gp_value = 0;
        KernelUid stack_block_uid = 0;
        uint32_t stack_base = 0;
        uint32_t initial_sp = 0;
        ThreadState state = ThreadState::kDormant;
        WaitType wait_type = WaitType::kNone;
        KernelUid wait_target_uid = 0;
        std::string wait_detail;
        int32_t wait_result = 0;
        bool wait_allow_callbacks = false;
        bool has_timeout = false;
        std::chrono::steady_clock::time_point deadline;
        int32_t wakeup_count = 0;
        int32_t exit_status = 0;
        bool delete_on_exit = false;
        bool spawned = false;
        std::vector<uint8_t> cpu_context;
        std::unique_ptr<platform::HostThread> host_thread;
        std::vector<KernelUid> callbacks;
    };

    std::shared_ptr<GuestThread> FindThreadLocked(KernelUid uid) const;
    void MakeReadyLocked(const std::shared_ptr<GuestThread>& t, bool at_front = false);
    void RemoveFromReadyQueueLocked(KernelUid uid);
    std::shared_ptr<GuestThread> PickNextReadyLocked();
    bool HasHigherPriorityReadyLocked(int32_t priority) const;
    void SwitchOutCurrentLocked(std::unique_lock<std::mutex>& lock, const std::shared_ptr<GuestThread>& cur);
    void DeliverPendingInterruptsLocked(std::unique_lock<std::mutex>& lock);
    void WakeWaitersLocked(WaitType type, KernelUid target_uid, int32_t result, bool wake_all);
    int32_t RunPendingCallbacksLocked(std::unique_lock<std::mutex>& lock, const std::shared_ptr<GuestThread>& cur);
    void ThreadEntry(std::shared_ptr<GuestThread> self);
    void ReportFatalStopLocked(const std::string& reason);
    std::string FormatDeadlockReportLocked() const;
    void FreeThreadStackLocked(const std::shared_ptr<GuestThread>& t);

    const repsp_guest_cpu_ops* ops_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::vector<std::shared_ptr<GuestThread>> threads_;
    std::vector<KernelUid> ready_queue_;
    std::vector<PendingInterrupt> pending_interrupts_;
    KernelUid active_thread_uid_ = 0;

    bool intr_enabled_ = true;
    bool in_interrupt_ = false;
    bool running_ = false;
    bool shutdown_ = false;
    bool fatal_stop_ = false;
    std::string fatal_stop_reason_;
    uint32_t external_wait_sources_ = 0;
};

Scheduler& GetScheduler();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_SCHEDULER_H
