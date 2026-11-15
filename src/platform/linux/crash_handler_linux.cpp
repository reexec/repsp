#include "../crash_handler_internal.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <unistd.h>

namespace repsp::platform
{

namespace
{

char g_report_buf[64 * 1024];
struct sigaction g_prev_segv{};
struct sigaction g_prev_bus{};
struct sigaction g_prev_ill{};
struct sigaction g_prev_fpe{};
struct sigaction g_prev_abrt{};
std::terminate_handler g_prev_terminate = nullptr;

void WriteLinuxCrashReport(const detail::CrashTerminationInfo& info)
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
        s.state_provider_cb(s.state_provider_user_data, &buf);
    }

    if (s.extra_cb != nullptr)
    {
        buf.Append("\n--- App Extra Info ---\n");
        s.extra_cb(s.extra_user_data, &buf);
    }

    const int fd = open(s.report_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        size_t offset = 0;
        while (offset < buf.length)
        {
            const ssize_t n = write(fd, buf.data + offset, buf.length - offset);
            if (n <= 0)
            {
                break;
            }
            offset += static_cast<size_t>(n);
        }
        close(fd);
    }
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
    WriteLinuxCrashReport(info);
}

const char* SignalName(int signum)
{
    switch (signum)
    {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default: return "SIGNAL";
    }
}

void CrashSignalHandler(int signum, siginfo_t* siginfo, void* /*context*/)
{
    detail::CrashTerminationInfo info{};
    info.kind = (signum == SIGABRT) ? "abort" : "linux_signal";
    info.code = static_cast<uint64_t>(signum);
    info.address = siginfo ? reinterpret_cast<uintptr_t>(siginfo->si_addr) : 0;
    info.detail = SignalName(signum);

    HandleFatalTermination(info);

    std::signal(signum, SIG_DFL);
    std::raise(signum);
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

    if (!s.installed.exchange(true))
    {
        struct sigaction sa{};
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = CrashSignalHandler;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, &g_prev_segv);
        sigaction(SIGBUS, &sa, &g_prev_bus);
        sigaction(SIGILL, &sa, &g_prev_ill);
        sigaction(SIGFPE, &sa, &g_prev_fpe);
        sigaction(SIGABRT, &sa, &g_prev_abrt);

        g_prev_terminate = std::set_terminate(CrashTerminateHandler);
    }
}

void UninstallCrashHandler()
{
    auto& s = detail::GetCrashSharedState();
    if (s.installed.exchange(false))
    {
        sigaction(SIGSEGV, &g_prev_segv, nullptr);
        sigaction(SIGBUS, &g_prev_bus, nullptr);
        sigaction(SIGILL, &g_prev_ill, nullptr);
        sigaction(SIGFPE, &g_prev_fpe, nullptr);
        sigaction(SIGABRT, &g_prev_abrt, nullptr);
        std::set_terminate(g_prev_terminate);
        g_prev_terminate = nullptr;
    }
    s.handling_crash.store(false);
}

} // namespace repsp::platform
