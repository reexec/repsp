#ifndef REPSP_PLATFORM_HOST_THREAD_H
#define REPSP_PLATFORM_HOST_THREAD_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace repsp::platform
{

class HostThread
{
public:
    HostThread(std::string name, std::function<void()> entry);
    ~HostThread();

    HostThread(const HostThread&) = delete;
    HostThread& operator=(const HostThread&) = delete;

    void Join();
    uint64_t GetCpuTimeNs() const;
    const std::string& GetName() const { return name_; }

    static void SetCurrentThreadName(const std::string& name);
    static uint64_t GetCurrentThreadCpuTimeNs();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string name_;
};

} // namespace repsp::platform

#endif // REPSP_PLATFORM_HOST_THREAD_H
