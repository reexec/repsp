#ifndef REPSP_PLATFORM_HOST_THREAD_H
#define REPSP_PLATFORM_HOST_THREAD_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace repsp::platform
{

struct HostThreadOptions
{
    // Reserved stack size in bytes (0 = OS default).
    size_t stack_reserve_bytes = 0;
};

struct HostStackInfo
{
    uintptr_t stack_low = 0;
    uintptr_t stack_high = 0;
    uintptr_t current_sp = 0;
    uint64_t committed_bytes = 0;
};

class HostThread
{
public:
    HostThread(std::string name, std::function<void()> entry);
    HostThread(std::string name, std::function<void()> entry, const HostThreadOptions& options);
    ~HostThread();

    HostThread(const HostThread&) = delete;
    HostThread& operator=(const HostThread&) = delete;

    void Join();
    uint64_t GetCpuTimeNs() const;
    const std::string& GetName() const { return name_; }

    static void SetCurrentThreadName(const std::string& name);
    static uint64_t GetCurrentThreadCpuTimeNs();
    static bool GetCurrentThreadStackInfo(HostStackInfo& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string name_;
};

} // namespace repsp::platform

#endif // REPSP_PLATFORM_HOST_THREAD_H
