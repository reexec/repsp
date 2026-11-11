#include "repsp/platform/host_thread.h"

#include <pthread.h>
#include <time.h>
#include <utility>

namespace repsp::platform
{

struct HostThread::Impl
{
    pthread_t thread = 0;
    std::function<void()> entry;
    bool joined = false;

    static void* ThreadProc(void* param)
    {
        auto* impl = static_cast<Impl*>(param);
        if (impl->entry)
        {
            impl->entry();
        }
        return nullptr;
    }
};

HostThread::HostThread(std::string name, std::function<void()> entry)
    : impl_(std::make_unique<Impl>()), name_(std::move(name))
{
    impl_->entry = std::move(entry);
    pthread_create(&impl_->thread, nullptr, &Impl::ThreadProc, impl_.get());
    if (!name_.empty())
    {
        // Linux limits thread name to 16 bytes including null
        std::string truncated = name_.substr(0, 15);
        pthread_setname_np(impl_->thread, truncated.c_str());
    }
}

HostThread::~HostThread()
{
    Join();
}

void HostThread::Join()
{
    if (!impl_->joined)
    {
        pthread_join(impl_->thread, nullptr);
        impl_->joined = true;
    }
}

uint64_t HostThread::GetCpuTimeNs() const
{
    clockid_t clock_id;
    if (pthread_getcpuclockid(impl_->thread, &clock_id) == 0)
    {
        struct timespec ts;
        if (clock_gettime(clock_id, &ts) == 0)
        {
            return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
        }
    }
    return 0;
}

void HostThread::SetCurrentThreadName(const std::string& name)
{
    if (!name.empty())
    {
        std::string truncated = name.substr(0, 15);
        pthread_setname_np(pthread_self(), truncated.c_str());
    }
}

uint64_t HostThread::GetCurrentThreadCpuTimeNs()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
    {
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }
    return 0;
}

} // namespace repsp::platform
