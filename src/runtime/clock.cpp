#include "repsp/runtime/clock.h"

#include <chrono>
#include <mutex>

namespace repsp::runtime
{

namespace
{

std::mutex& ClockMutex()
{
    static std::mutex m;
    return m;
}

std::chrono::steady_clock::time_point& BootTime()
{
    static std::chrono::steady_clock::time_point tp = std::chrono::steady_clock::now();
    return tp;
}

} // namespace

uint64_t GetSystemTimeWide()
{
    std::lock_guard<std::mutex> lock(ClockMutex());
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - BootTime()).count();
    return us > 0 ? static_cast<uint64_t>(us) : 0;
}

uint32_t GetSystemTimeLow()
{
    return static_cast<uint32_t>(GetSystemTimeWide() & 0xFFFFFFFFu);
}

void ResetBootClock()
{
    std::lock_guard<std::mutex> lock(ClockMutex());
    BootTime() = std::chrono::steady_clock::now();
}

} // namespace repsp::runtime
