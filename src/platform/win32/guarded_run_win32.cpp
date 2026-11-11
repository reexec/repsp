#include "repsp/platform/guarded_run.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <malloc.h>

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
    else
    {
        result.succeeded = false;
        result.native_exception_code = info.code;
        result.fault_address = static_cast<uintptr_t>(info.fault_address);
        result.description = CodeToDescription(info.code);
    }

    return result;
}

} // namespace repsp::platform
