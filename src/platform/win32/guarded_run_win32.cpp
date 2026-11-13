#include "repsp/platform/guarded_run.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <malloc.h>

namespace repsp::platform
{

namespace
{

constexpr DWORD kExceptionCodeStop = 0xE0000001;

struct CrashInfo
{
    DWORD code = 0;
    ULONG_PTR fault_address = 0;
    bool is_stop = false;
    const char* stop_reason = nullptr;
};

int ExceptionFilter(unsigned int code, struct _EXCEPTION_POINTERS* ep, CrashInfo* info)
{
    if (code == kExceptionCodeStop)
    {
        if (info != nullptr)
        {
            info->code = code;
            info->is_stop = true;
            if (ep != nullptr && ep->ExceptionRecord != nullptr &&
                ep->ExceptionRecord->NumberParameters >= 1)
            {
                info->stop_reason = reinterpret_cast<const char*>(ep->ExceptionRecord->ExceptionInformation[0]);
            }
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }

    bool is_hardware_fault = false;

    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        is_hardware_fault = true;
        break;
    default:
        is_hardware_fault = false;
        break;
    }

    if (!is_hardware_fault)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (code == EXCEPTION_STACK_OVERFLOW)
    {
        _resetstkoflw();
    }

    if (info != nullptr)
    {
        info->code = code;
        if (ep != nullptr && ep->ExceptionRecord != nullptr)
        {
            if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
                ep->ExceptionRecord->NumberParameters >= 2)
            {
                info->fault_address = ep->ExceptionRecord->ExceptionInformation[1];
            }
            else
            {
                info->fault_address = reinterpret_cast<ULONG_PTR>(ep->ExceptionRecord->ExceptionAddress);
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

const char* CodeToDescription(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "Access violation";
    case EXCEPTION_IN_PAGE_ERROR:
        return "In-page error";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "Illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "Privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "Integer divide by zero";
    case EXCEPTION_INT_OVERFLOW:
        return "Integer overflow";
    case EXCEPTION_STACK_OVERFLOW:
        return "Stack overflow";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "Datatype misalignment";
    default:
        return "Hardware fault";
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
    else if (info.is_stop)
    {
        result.succeeded = false;
        result.stopped = true;
        result.stop_reason = info.stop_reason ? info.stop_reason : "";
        result.description = "Execution stopped: " + result.stop_reason;
    }
    else
    {
        result.succeeded = false;
        result.native_exception_code = info.code;
        result.fault_address = static_cast<uintptr_t>(info.fault_address);
        result.description = CodeToDescription(info.code);
    }

    return result;
}

[[noreturn]] void RaiseStop(const char* reason)
{
    static thread_local char stop_reason_buf[512];
    if (reason != nullptr)
    {
        std::snprintf(stop_reason_buf, sizeof(stop_reason_buf), "%s", reason);
    }
    else
    {
        stop_reason_buf[0] = '\0';
    }
    ULONG_PTR args[1] = { reinterpret_cast<ULONG_PTR>(stop_reason_buf) };
    RaiseException(kExceptionCodeStop, EXCEPTION_NONCONTINUABLE, 1, args);
    std::abort();
}

} // namespace repsp::platform

extern "C" void repsp_raise_stop(const char* reason)
{
    repsp::platform::RaiseStop(reason);
}
