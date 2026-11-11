#include "repsp/platform/host_thread.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <utility>

namespace repsp::platform
{

namespace
{

uint64_t FileTimeToNs(const FILETIME& ft)
{
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart * 100ULL;
}

} // namespace

struct HostThread::Impl
{
    HANDLE handle = nullptr;
    std::function<void()> entry;
    bool joined = false;

    static DWORD WINAPI ThreadProc(LPVOID param)
    {
        auto* impl = static_cast<Impl*>(param);
        if (impl->entry)
        {
            impl->entry();
        }
        return 0;
    }
};

HostThread::HostThread(std::string name, std::function<void()> entry)
    : impl_(std::make_unique<Impl>()), name_(std::move(name))
{
    impl_->entry = std::move(entry);
    impl_->handle = CreateThread(nullptr, 0, &Impl::ThreadProc, impl_.get(), 0, nullptr);
    if (impl_->handle != nullptr && !name_.empty())
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, name_.c_str(), -1, nullptr, 0);
        if (wlen > 0)
        {
            std::wstring wname(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, name_.c_str(), -1, wname.data(), wlen);
            SetThreadDescription(impl_->handle, wname.c_str());
        }
    }
}

HostThread::~HostThread()
{
    Join();
    if (impl_->handle != nullptr)
    {
        CloseHandle(impl_->handle);
        impl_->handle = nullptr;
    }
}

void HostThread::Join()
{
    if (impl_->handle != nullptr && !impl_->joined)
    {
        WaitForSingleObject(impl_->handle, INFINITE);
        impl_->joined = true;
    }
}

uint64_t HostThread::GetCpuTimeNs() const
{
    if (impl_->handle == nullptr)
    {
        return 0;
    }

    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    if (GetThreadTimes(impl_->handle, &creation_time, &exit_time, &kernel_time, &user_time))
    {
        return FileTimeToNs(kernel_time) + FileTimeToNs(user_time);
    }
    return 0;
}

void HostThread::SetCurrentThreadName(const std::string& name)
{
    if (name.empty())
    {
        return;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
    if (wlen > 0)
    {
        std::wstring wname(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wname.data(), wlen);
        SetThreadDescription(GetCurrentThread(), wname.c_str());
    }
}

uint64_t HostThread::GetCurrentThreadCpuTimeNs()
{
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    if (GetThreadTimes(GetCurrentThread(), &creation_time, &exit_time, &kernel_time, &user_time))
    {
        return FileTimeToNs(kernel_time) + FileTimeToNs(user_time);
    }
    return 0;
}

} // namespace repsp::platform
