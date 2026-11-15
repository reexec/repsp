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
    : HostThread(std::move(name), std::move(entry), HostThreadOptions{})
{
}

HostThread::HostThread(std::string name, std::function<void()> entry, const HostThreadOptions& options)
    : impl_(std::make_unique<Impl>()), name_(std::move(name))
{
    impl_->entry = std::move(entry);
    const DWORD flags = (options.stack_reserve_bytes > 0) ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0;
    impl_->handle = CreateThread(
        nullptr,
        static_cast<SIZE_T>(options.stack_reserve_bytes),
        &Impl::ThreadProc,
        impl_.get(),
        flags,
        nullptr);
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

bool HostThread::GetCurrentThreadStackInfo(HostStackInfo& out)
{
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    if (high <= low)
    {
        return false;
    }

    volatile uint8_t probe = 0;
    (void)probe;
    out.stack_low = static_cast<uintptr_t>(low);
    out.stack_high = static_cast<uintptr_t>(high);
    out.current_sp = reinterpret_cast<uintptr_t>(const_cast<uint8_t*>(&probe));

    uint64_t committed = 0;
    ULONG_PTR cursor = low;
    while (cursor < high)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) == 0 || mbi.RegionSize == 0)
        {
            break;
        }
        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_GUARD) == 0)
        {
            const ULONG_PTR region_start = reinterpret_cast<ULONG_PTR>(mbi.BaseAddress);
            const ULONG_PTR region_end = region_start + mbi.RegionSize;
            const ULONG_PTR clamped_end = (region_end < high) ? region_end : high;
            if (clamped_end > cursor)
            {
                committed += static_cast<uint64_t>(clamped_end - cursor);
            }
        }
        cursor = reinterpret_cast<ULONG_PTR>(mbi.BaseAddress) + mbi.RegionSize;
    }
    out.committed_bytes = committed;
    return true;
}

} // namespace repsp::platform
