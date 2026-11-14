#include "repsp/runtime/vblank_clock.h"
#include "repsp/runtime/scheduler.h"

#include <algorithm>
#include <chrono>

namespace repsp::runtime
{

namespace
{

constexpr auto kVblankPeriod = std::chrono::microseconds(16683); // ~59.94006 Hz (PSP VBlank)
constexpr KernelUid kVblankWaitUid = 0x56424C4Bu;               // 'VBLK'

} // namespace

VblankClock::~VblankClock()
{
    ResetForTest();
}

void VblankClock::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++start_ref_count_;
    if (running_)
    {
        return;
    }
    stop_requested_ = false;
    running_ = true;
    thread_ = std::thread(&VblankClock::TimerThreadMain, this);
}

void VblankClock::Stop()
{
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (start_ref_count_ > 0)
        {
            --start_ref_count_;
        }
        if (start_ref_count_ > 0 || !running_)
        {
            return;
        }
        stop_requested_ = true;
        running_ = false;
        stop_cv_.notify_all();
        to_join = std::move(thread_);
    }

    if (to_join.joinable())
    {
        to_join.join();
    }
}

bool VblankClock::IsRunning() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

uint32_t VblankClock::Count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return vblank_count_;
}

uint32_t VblankClock::Subscribe(SubscriberFn fn)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t id = next_subscriber_id_++;
    subscribers_.push_back(SubscriberEntry{id, std::move(fn)});
    return id;
}

uint32_t VblankClock::Subscribe(std::function<void()> fn)
{
    return Subscribe([cb = std::move(fn)](uint32_t)
    {
        if (cb)
        {
            cb();
        }
    });
}

void VblankClock::Unsubscribe(uint32_t id)
{
    if (id == 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(
        std::remove_if(subscribers_.begin(), subscribers_.end(),
                       [id](const SubscriberEntry& e) { return e.id == id; }),
        subscribers_.end());
}

int32_t VblankClock::WaitNext(bool allow_callbacks)
{
    auto& sched = GetScheduler();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++waiters_;
        if (waiters_ == 1)
        {
            sched.SetExternalWaitSources(1);
        }
    }

    // Hold no clock mutex while blocking in the scheduler (stop/wait lock contract).
    const int32_t res = sched.WaitCurrent(
        WaitType::kCustom,
        kVblankWaitUid,
        "WaitVblank",
        nullptr,
        allow_callbacks);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (waiters_ > 0)
        {
            --waiters_;
            if (waiters_ == 0)
            {
                sched.SetExternalWaitSources(0);
            }
        }
    }

    return res;
}

void VblankClock::StepForTest()
{
    DispatchTick();
}

void VblankClock::ResetForTest()
{
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
        running_ = false;
        start_ref_count_ = 0;
        stop_cv_.notify_all();
        to_join = std::move(thread_);
    }

    if (to_join.joinable())
    {
        to_join.join();
    }

    bool had_waiters = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        vblank_count_ = 0;
        had_waiters = (waiters_ > 0);
        waiters_ = 0;
        next_subscriber_id_ = 1;
        subscribers_.clear();
        stop_requested_ = false;
    }

    if (had_waiters)
    {
        GetScheduler().SetExternalWaitSources(0);
    }
}

void VblankClock::TimerThreadMain()
{
    auto next_tick = std::chrono::steady_clock::now() + kVblankPeriod;
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_cv_.wait_until(lock, next_tick, [this]() { return stop_requested_; });
            if (stop_requested_)
            {
                break;
            }
        }

        next_tick += kVblankPeriod;
        const auto now = std::chrono::steady_clock::now();
        if (next_tick < now)
        {
            next_tick = now + kVblankPeriod;
        }

        DispatchTick();
    }
}

void VblankClock::DispatchTick()
{
    uint32_t vcount = 0;
    std::vector<SubscriberEntry> subs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++vblank_count_;
        vcount = vblank_count_;
        subs = subscribers_;
    }

    // Invoke subscribers outside mutex_ in registration order before waking waiters.
    for (const auto& entry : subs)
    {
        if (entry.fn)
        {
            entry.fn(vcount);
        }
    }

    GetScheduler().WakeAllWaiters(WaitType::kCustom, kVblankWaitUid, 0);
}

VblankClock& GetVblankClock()
{
    static VblankClock clock;
    return clock;
}

} // namespace repsp::runtime
