#include "../crash_handler_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace repsp::platform
{

namespace
{

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(
    HANDLE,
    DWORD,
    HANDLE,
    MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);

char g_report_buf[64 * 1024];
HMODULE g_dbghelp_module = nullptr;
MiniDumpWriteDumpFn g_mini_dump_write_dump = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_prev_exception_filter = nullptr;
std::terminate_handler g_prev_terminate = nullptr;
void (*g_prev_sigabrt)(int) = SIG_DFL;

void InvokeCallbackGuarded(CrashExtraCallback cb, void* user_data, CrashTextBuffer* out)
{
    if (cb == nullptr)
    {
        return;
    }
    __try
    {
        cb(user_data, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out->Append("\n[crash_handler] callback faulted; continuing\n");
    }
}

BOOL CALLBACK MinidumpFilterCallback(
    PVOID callback_param,
    PMINIDUMP_CALLBACK_INPUT input,
    PMINIDUMP_CALLBACK_OUTPUT output)
{
    if (input == nullptr || output == nullptr)
    {
        return FALSE;
    }
    if (input->CallbackType == RemoveMemoryCallback)
    {
        bool* already_removed = static_cast<bool*>(callback_param);
        if (already_removed != nullptr && *already_removed)
        {
            return FALSE;
        }
        const auto& s = detail::GetCrashSharedState();
        if (s.guest_mem_size > 0 && s.guest_mem_base != 0)
        {
            if (already_removed != nullptr)
            {
                *already_removed = true;
            }
            output->MemoryBase = static_cast<ULONG64>(s.guest_mem_base);
            output->MemorySize = static_cast<ULONG>(s.guest_mem_size);
            return TRUE;
        }
        return FALSE;
    }
    return TRUE;
}

void WriteWin32CrashReport(const detail::CrashTerminationInfo& info)
{
    auto& s = detail::GetCrashSharedState();
    if (s.report_path[0] == '\0')
    {
        return;
    }

    CrashTextBuffer buf{};
    buf.data = g_report_buf;
    buf.capacity = sizeof(g_report_buf);
    buf.length = 0;
    g_report_buf[0] = '\0';

    detail::FormatCrashReportHeader(&buf, info, GetCurrentHostThreadId());

    if (s.state_provider_cb != nullptr)
    {
        buf.Append("\n--- Runtime / Scheduler State ---\n");
        InvokeCallbackGuarded(s.state_provider_cb, s.state_provider_user_data, &buf);
    }

    if (s.extra_cb != nullptr)
    {
        buf.Append("\n--- App Extra Info ---\n");
        InvokeCallbackGuarded(s.extra_cb, s.extra_user_data, &buf);
    }

    HANDLE hfile = CreateFileA(
        s.report_path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hfile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(hfile, buf.data, static_cast<DWORD>(buf.length), &written, nullptr);
        CloseHandle(hfile);
    }
}

void WriteWin32Minidump(EXCEPTION_POINTERS* ep)
{
    const auto& s = detail::GetCrashSharedState();
    if (!s.write_minidump || s.minidump_path[0] == '\0' || g_mini_dump_write_dump == nullptr)
    {
        return;
    }

    HANDLE hfile = CreateFileA(
        s.minidump_path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hfile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    PMINIDUMP_EXCEPTION_INFORMATION mei_ptr = nullptr;
    if (ep != nullptr)
    {
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        mei_ptr = &mei;
    }

    bool guest_mem_removed = false;
    MINIDUMP_CALLBACK_INFORMATION mci{};
    mci.CallbackRoutine = MinidumpFilterCallback;
    mci.CallbackParam = &guest_mem_removed;

    const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithHandleData);

    g_mini_dump_write_dump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        hfile,
        dump_type,
        mei_ptr,
        nullptr,
        &mci);

    CloseHandle(hfile);
}

void HandleFatalTermination(const detail::CrashTerminationInfo& info)
{
    auto& s = detail::GetCrashSharedState();
    if (s.handling_crash.exchange(true))
    {
        return;
    }

    detail::RunCrashCleanupCallbacks();
    FlushLogFile();
    WriteWin32CrashReport(info);
    WriteWin32Minidump(static_cast<EXCEPTION_POINTERS*>(info.native_exception_pointers));
}

LONG WINAPI CrashUnhandledExceptionFilter(EXCEPTION_POINTERS* ep)
{
    detail::CrashTerminationInfo info{};
    info.kind = "win32_unhandled_exception";
    if (ep != nullptr && ep->ExceptionRecord != nullptr)
    {
        info.code = static_cast<uint64_t>(ep->ExceptionRecord->ExceptionCode);
        info.address = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    }
    info.native_exception_pointers = ep;

    HandleFatalTermination(info);

    if (g_prev_exception_filter != nullptr)
    {
        return g_prev_exception_filter(ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void CrashAbortHandler(int sig)
{
    detail::CrashTerminationInfo info{};
    info.kind = "abort";
    info.code = static_cast<uint64_t>(sig);
    info.detail = "SIGABRT / abort()";

    HandleFatalTermination(info);

    std::signal(SIGABRT, g_prev_sigabrt);
    if (g_prev_sigabrt != SIG_DFL && g_prev_sigabrt != SIG_IGN && g_prev_sigabrt != nullptr)
    {
        g_prev_sigabrt(sig);
    }
}

void CrashTerminateHandler()
{
    char detail_buf[512]{};
    detail::CrashTerminationInfo info{};
    info.kind = "std_terminate";

    if (const std::exception_ptr eptr = std::current_exception())
    {
        try
        {
            std::rethrow_exception(eptr);
        }
        catch (const std::exception& ex)
        {
            info.kind = "unhandled_cpp_exception";
            std::snprintf(detail_buf, sizeof(detail_buf), "%s", ex.what() ? ex.what() : "(null what)");
            info.detail = detail_buf;
        }
        catch (...)
        {
            info.kind = "unhandled_cpp_exception";
            info.detail = "unknown non-std::exception";
        }
    }
    else
    {
        info.detail = "std::terminate called without active exception";
    }

    HandleFatalTermination(info);

    if (g_prev_terminate != nullptr)
    {
        g_prev_terminate();
    }
    std::abort();
}

} // namespace

void InstallCrashHandler(const CrashConfig& config)
{
    auto& s = detail::GetCrashSharedState();
    detail::PrepareCrashOutputPaths(config.crash_dir.c_str());
    s.write_minidump = config.write_minidump;
    if (config.extra_callback != nullptr)
    {
        s.extra_cb = config.extra_callback;
        s.extra_user_data = config.extra_user_data;
    }

    if (g_dbghelp_module == nullptr && config.write_minidump)
    {
        g_dbghelp_module = LoadLibraryA("dbghelp.dll");
        if (g_dbghelp_module != nullptr)
        {
            FARPROC proc = GetProcAddress(g_dbghelp_module, "MiniDumpWriteDump");
            std::memcpy(&g_mini_dump_write_dump, &proc, sizeof(g_mini_dump_write_dump));
        }
    }

    if (!s.installed.exchange(true))
    {
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        g_prev_exception_filter = SetUnhandledExceptionFilter(CrashUnhandledExceptionFilter);
        g_prev_terminate = std::set_terminate(CrashTerminateHandler);
        g_prev_sigabrt = std::signal(SIGABRT, CrashAbortHandler);
    }
}

void UninstallCrashHandler()
{
    auto& s = detail::GetCrashSharedState();
    if (s.installed.exchange(false))
    {
        SetUnhandledExceptionFilter(g_prev_exception_filter);
        g_prev_exception_filter = nullptr;
        std::set_terminate(g_prev_terminate);
        g_prev_terminate = nullptr;
        std::signal(SIGABRT, g_prev_sigabrt);
        g_prev_sigabrt = SIG_DFL;
    }
    s.handling_crash.store(false);
}

} // namespace repsp::platform
