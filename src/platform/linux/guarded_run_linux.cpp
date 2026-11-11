#include "repsp/platform/guarded_run.h"

#include <csignal>
#include <csetjmp>

namespace repsp::platform
{

namespace
{

thread_local sigjmp_buf g_jump_buffer;
thread_local volatile sig_atomic_t g_in_guarded_run = 0;
thread_local volatile sig_atomic_t g_last_signal = 0;
thread_local uintptr_t g_fault_address = 0;

void SignalHandler(int signum, siginfo_t* info, void* /*context*/)
{
    if (g_in_guarded_run)
    {
        g_last_signal = signum;
        if (info != nullptr)
        {
            g_fault_address = reinterpret_cast<uintptr_t>(info->si_addr);
        }
        siglongjmp(g_jump_buffer, 1);
    }
}

} // namespace

GuardedRunResult GuardedRun(const std::function<void()>& fn)
{
    GuardedRunResult result;

    struct sigaction sa;
    struct sigaction old_segv;
    struct sigaction old_bus;

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = SignalHandler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);

    g_last_signal = 0;
    g_fault_address = 0;
    g_in_guarded_run = 1;

    if (sigsetjmp(g_jump_buffer, 1) == 0)
    {
        fn();
        result.succeeded = true;
    }
    else
    {
        result.succeeded = false;
        result.native_exception_code = static_cast<uint32_t>(g_last_signal);
        result.fault_address = g_fault_address;
        result.description = (g_last_signal == SIGSEGV) ? "SIGSEGV" : "SIGBUS";
    }

    g_in_guarded_run = 0;
    sigaction(SIGSEGV, &old_segv, nullptr);
    sigaction(SIGBUS, &old_bus, nullptr);

    return result;
}

} // namespace repsp::platform
