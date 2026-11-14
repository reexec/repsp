#ifndef REPSP_RUNTIME_VBLANK_CLOCK_H
#define REPSP_RUNTIME_VBLANK_CLOCK_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>

namespace repsp::runtime
{

// Shared 59.94 Hz (16,683 us) VBlank clock for display and controller HLE.
// Subscribers run on each tick in registration order before waiting guest threads wake.
class VblankClock
{
public:
    using SubscriberFn = std::function<void(uint32_t vcount)>;

    VblankClock() = default;
    ~VblankClock();

    VblankClock(const VblankClock&) = delete;
    VblankClock& operator=(const VblankClock&) = delete;

    // Reference-counted timer thread lifecycle. First Start() spawns the thread;
    // the matching final Stop() stops and joins it.
    void Start();
    void Stop();
    bool IsRunning() const;

    // Current monotonic VBlank tick count since creation or ResetForTest().
    uint32_t Count() const;

    // Register or remove a per-tick callback invoked on the clock thread (or StepForTest).
    uint32_t Subscribe(SubscriberFn fn);
    uint32_t Subscribe(std::function<void()> fn);
    void Unsubscribe(uint32_t id);

    // Block the current guest thread until the next VBlank tick (manages Scheduler external wait sources).
    int32_t WaitNext(bool allow_callbacks);

    // Deterministically advance one tick without requiring the background timer thread.
    void StepForTest();

    // Stop any running timer thread and clear counters, waiters, and subscribers (for probes/tests).
    void ResetForTest();

private:
    struct SubscriberEntry
    {
        uint32_t id = 0;
        SubscriberFn fn;
    };

    void TimerThreadMain();
    void DispatchTick();

    mutable std::mutex mutex_;
    std::condition_variable stop_cv_;
    std::thread thread_;
    bool stop_requested_ = false;
    bool running_ = false;
    uint32_t start_ref_count_ = 0;
    uint32_t vblank_count_ = 0;
    uint32_t waiters_ = 0;
    uint32_t next_subscriber_id_ = 1;
    std::vector<SubscriberEntry> subscribers_;
};

VblankClock& GetVblankClock();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_VBLANK_CLOCK_H
