#include "repsp/platform/guarded_run.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace repsp::platform
{

namespace
{

struct CrashInfo
{
    DWORD code = 0;
    ULONG_PTR fault_address = 0;
};

int ExceptionFilter(unsigned int code, struct _EXCEPTION_POINTERS* ep, CrashInfo* info)
{
    if (info != nullptr)
    {
        info->code = code;
        if (ep != nullptr && ep->ExceptionRecord != nullptr)
        {
            if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
            {
                info->fault_address = ep->ExceptionRecord->ExceptionInformation[1];
            }
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

bool RunGuardedRaw(void (*callback)(void*), void* user_data, CrashInfo* info)
{
    __try
    {
        callback(user_data);
        return true;
    }
    __except (ExceptionFilter(GetExceptionCode(), GetExceptionInformation(), info))
    {
        return false;
    }
}

} // namespace

GuardedRunResult GuardedRun(const std::function<void()>& fn)
{
    GuardedRunResult result;
    CrashInfo info;

    auto trampoline = [](void* ud)
    {
        const auto* func = static_cast<const std::function<void()>*>(ud);
        (*func)();
    };

    if (RunGuardedRaw(trampoline, const_cast<void*>(static_cast<const void*>(&fn)), &info))
    {
        result.succeeded = true;
    }
    else
    {
        result.succeeded = false;
        result.native_exception_code = info.code;
        result.fault_address = static_cast<uintptr_t>(info.fault_address);
        if (info.code == EXCEPTION_ACCESS_VIOLATION)
        {
            result.description = "Access violation";
        }
        else
        {
            result.description = "Structured exception";
        }
    }

    return result;
}

} // namespace repsp::platform
