#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/hle/umd.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/vfs.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace repsp::hle::umd
{

namespace
{

class UmdController
{
public:
    UmdController() = default;

    ~UmdController()
    {
        StopTimerThread();
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        replace_permitted_ = false;
        drive_stat_ = kUmdDefaultReadyStat;
        transition_pending_ = false;
        ready_delay_ms_ = kDefaultReadyDelayMs;
        ++generation_;
        cv_.notify_all();
    }

    bool IsReplacePermitted() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return replace_permitted_;
    }

    void SetReplacePermitted(bool permitted)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        replace_permitted_ = permitted;
    }

    uint32_t GetDriveStat()
    {
        bool promoted = false;
        uint32_t stat = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            promoted = AdvanceStateLocked();
            stat = drive_stat_;
        }
        if (promoted)
        {
            runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kUmd, 0, 0);
        }
        return stat;
    }

    bool IsDiscPresent()
    {
        return (GetDriveStat() & kUmdNotPresent) == 0;
    }

    uint64_t GetGeneration() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_;
    }

    void SetReadyDelayMs(uint32_t delay_ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_delay_ms_ = delay_ms;
    }

    uint32_t GetReadyDelayMs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ready_delay_ms_;
    }

    SwapResult EjectDisc()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!replace_permitted_)
            {
                return {false, "UMD replacement is not permitted by the game"};
            }
            runtime::GetVfs().Unmount("disc0");
            runtime::GetVfs().Unmount("umd0");
            drive_stat_ = kUmdNotPresent;
            transition_pending_ = false;
            ++generation_;
            cv_.notify_all();
        }
        runtime::Log(runtime::LogLevel::kInfo, "[UMD] Disc ejected (stat=0x%02X)", kUmdNotPresent);
        runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kUmd, 0, 0);
        return {true, {}};
    }

    SwapResult InsertDisc(const std::filesystem::path& disc_root)
    {
        std::error_code ec;
        if (disc_root.empty() || !std::filesystem::exists(disc_root, ec) ||
            !std::filesystem::is_directory(disc_root, ec))
        {
            return {false, "Disc directory does not exist"};
        }

        uint32_t stat_after = 0;
        uint32_t delay_ms = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!replace_permitted_)
            {
                return {false, "UMD replacement is not permitted by the game"};
            }
            runtime::GetVfs().Mount("disc0", disc_root, true);
            runtime::GetVfs().Mount("umd0", disc_root, true);
            ++generation_;
            delay_ms = ready_delay_ms_;
            if (delay_ms == 0)
            {
                drive_stat_ = kUmdDefaultReadyStat;
                transition_pending_ = false;
            }
            else
            {
                drive_stat_ = kUmdPresent | kUmdChanged;
                transition_pending_ = true;
                ready_at_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
                EnsureTimerThreadLocked();
            }
            stat_after = drive_stat_;
            cv_.notify_all();
        }
        runtime::Log(runtime::LogLevel::kInfo, "[UMD] Disc inserted: %s (stat=0x%02X, ready_delay=%u ms)",
                     disc_root.string().c_str(), stat_after, delay_ms);
        runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kUmd, 0, 0);
        return {true, {}};
    }

private:
    bool AdvanceStateLocked()
    {
        if (transition_pending_ && std::chrono::steady_clock::now() >= ready_at_)
        {
            drive_stat_ = kUmdDefaultReadyStat;
            transition_pending_ = false;
            return true;
        }
        return false;
    }

    void EnsureTimerThreadLocked()
    {
        if (!timer_thread_.joinable())
        {
            stop_timer_ = false;
            timer_thread_ = std::thread(&UmdController::TimerLoop, this);
        }
    }

    void StopTimerThread()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_timer_ = true;
            transition_pending_ = false;
        }
        cv_.notify_all();
        if (timer_thread_.joinable())
        {
            timer_thread_.join();
        }
    }

    void TimerLoop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stop_timer_)
        {
            if (!transition_pending_)
            {
                cv_.wait(lock, [this]() { return stop_timer_ || transition_pending_; });
                continue;
            }
            const auto target = ready_at_;
            cv_.wait_until(lock, target, [this, target]() {
                return stop_timer_ || !transition_pending_ || ready_at_ != target;
            });
            if (stop_timer_)
            {
                break;
            }
            if (AdvanceStateLocked())
            {
                lock.unlock();
                runtime::Log(runtime::LogLevel::kInfo, "[UMD] Disc ready (stat=0x%02X)", kUmdDefaultReadyStat);
                runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kUmd, 0, 0);
                lock.lock();
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread timer_thread_;
    bool stop_timer_ = false;
    bool replace_permitted_ = false;
    uint32_t drive_stat_ = kUmdDefaultReadyStat;
    bool transition_pending_ = false;
    std::chrono::steady_clock::time_point ready_at_{};
    uint32_t ready_delay_ms_ = kDefaultReadyDelayMs;
    uint64_t generation_ = 0;
};

UmdController& GetController()
{
    static UmdController controller;
    return controller;
}

} // namespace

bool IsReplacePermitted()
{
    return GetController().IsReplacePermitted();
}

uint32_t GetDriveStat()
{
    return GetController().GetDriveStat();
}

SwapResult EjectDisc()
{
    return GetController().EjectDisc();
}

SwapResult InsertDisc(const std::filesystem::path& disc_root)
{
    return GetController().InsertDisc(disc_root);
}

void SetReadyDelayMs(uint32_t delay_ms)
{
    GetController().SetReadyDelayMs(delay_ms);
}

uint32_t GetReadyDelayMs()
{
    return GetController().GetReadyDelayMs();
}

} // namespace repsp::hle::umd

namespace repsp::hle::kernel::detail
{

namespace
{

int32_t WaitDriveStatLoop(uint32_t wanted_stat, bool has_timeout, uint32_t timeout_us, const char* wait_name)
{
    const auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        if ((umd::GetDriveStat() & wanted_stat) != 0)
        {
            return 0;
        }
        uint32_t rem_us = 0;
        const uint32_t* timeout_ptr = nullptr;
        if (has_timeout)
        {
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed_us >= static_cast<int64_t>(timeout_us))
            {
                return kSceKernelErrorWaitTimeout;
            }
            rem_us = static_cast<uint32_t>(static_cast<int64_t>(timeout_us) - elapsed_us);
            timeout_ptr = &rem_us;
        }

        const int32_t res = runtime::GetScheduler().WaitCurrentIf(
            runtime::WaitType::kUmd,
            0,
            wait_name,
            timeout_ptr,
            false,
            [wanted_stat]() { return (umd::GetDriveStat() & wanted_stat) == 0; });
        if (res != 0)
        {
            return res;
        }
    }
}

void HleUmdActivate()
{
    recomp::Return(0);
}

void HleUmdDeactivate()
{
    recomp::Return(0);
}

void HleUmdGetDriveStat()
{
    recomp::Return(umd::GetDriveStat());
}

// PSPSDK pspumd.h: sceUmdCheckMedium returns non-zero (1) if a disc is present, 0 otherwise.
void HleUmdCheckMedium()
{
    recomp::Return(IsUmdDiscPresent() ? 1u : 0u);
}

void HleUmdReplacePermit()
{
    umd::GetController().SetReplacePermitted(true);
    runtime::Log(runtime::LogLevel::kInfo, "[UMD] sceUmdReplacePermit()");
    recomp::Return(0);
}

void HleUmdReplaceProhibit()
{
    umd::GetController().SetReplacePermitted(false);
    runtime::Log(runtime::LogLevel::kInfo, "[UMD] sceUmdReplaceProhibit()");
    recomp::Return(0);
}

void HleUmdWaitDriveStat()
{
    const uint32_t stat = recomp::Arg<uint32_t>(0);
    recomp::Return(WaitDriveStatLoop(stat, false, 0, "sceUmdWaitDriveStat"));
}

void HleUmdWaitDriveStatWithTimer()
{
    const uint32_t stat = recomp::Arg<uint32_t>(0);
    const uint32_t timeout = recomp::Arg<uint32_t>(1);
    recomp::Return(WaitDriveStatLoop(stat, true, timeout, "sceUmdWaitDriveStatWithTimer"));
}

void HleUmdCancelWaitDriveStat()
{
    runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kUmd, 0, kSceKernelErrorWaitCancel);
    recomp::Return(0);
}

} // namespace

void ResetUmd()
{
    umd::GetController().Reset();
}

bool IsUmdDiscPresent()
{
    return umd::GetController().IsDiscPresent();
}

uint64_t GetUmdGeneration()
{
    return umd::GetController().GetGeneration();
}

void RegisterUmd()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceUmdUser", "sceUmdActivate", HleUmdActivate);
    reg.RegisterByName("sceUmdUser", "sceUmdDeactivate", HleUmdDeactivate);
    reg.RegisterByName("sceUmdUser", "sceUmdGetDriveStat", HleUmdGetDriveStat);
    reg.RegisterByName("sceUmdUser", "sceUmdCheckMedium", HleUmdCheckMedium);
    reg.RegisterByName("sceUmdUser", "sceUmdReplacePermit", HleUmdReplacePermit);
    reg.RegisterByName("sceUmdUser", "sceUmdReplaceProhibit", HleUmdReplaceProhibit);
    reg.RegisterByName("sceUmdUser", "sceUmdWaitDriveStat", HleUmdWaitDriveStat);
    reg.RegisterByName("sceUmdUser", "sceUmdWaitDriveStatWithTimer", HleUmdWaitDriveStatWithTimer);
    reg.RegisterByName("sceUmdUser", "sceUmdCancelWaitDriveStat", HleUmdCancelWaitDriveStat);
}

} // namespace repsp::hle::kernel::detail
