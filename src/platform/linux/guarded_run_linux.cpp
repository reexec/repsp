#include "repsp/platform/guarded_run.h"

#include <csignal>
#include <csetjmp>
#include <cstdlib>
#include <mutex>

namespace repsp::platform
{

namespace
{

// A caught fault is never resumed and is used strictly for stop reporting.
// siglongjmp unwinds execution without invoking C++ destructors between
// the call site and the faulting instruction.

struct GuardFrame
{
    sigjmp_buf jmp_buf;
    volatile sig_atomic_t signal_num = 0;
    uintptr_t fault_address = 0;
    bool is_stop = false;
    const char* stop_reason = nullptr;
};

constexpr size_t kMaxGuardDepth = 16;
thread_local GuardFrame g_guard_stack[kMaxGuardDepth];
thread_local size_t g_guard_depth = 0;

struct sigaction g_prev_segv;
struct sigaction g_prev_bus;
struct sigaction g_prev_ill;
struct sigaction g_prev_fpe;
std::once_flag g_handlers_installed;

void ForwardSignal(int signum, siginfo_t* info, void* context)
{
    struct sigaction* prev = nullptr;
    switch (signum)
    {
    case SIGSEGV:
        prev = &g_prev_segv;
        break;
    case SIGBUS:
        prev = &g_prev_bus;
        break;
    case SIGILL:
        prev = &g_prev_ill;
        break;
    case SIGFPE:
        prev = &g_prev_fpe;
        break;
    default:
        break;
    }

    if (prev != nullptr)
    {
        if ((prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction != nullptr)
        {
            prev->sa_sigaction(signum, info, context);
            return;
        }
        if (!(prev->sa_flags & SA_SIGINFO) && prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN && prev->sa_handler != nullptr)
        {
            prev->sa_handler(signum);
            return;
        }
        if (prev->sa_handler == SIG_IGN)
        {
            return;
        }
    }

    std::signal(signum, SIG_DFL);
    std::raise(signum);
}

void GlobalSignalHandler(int signum, siginfo_t* info, void* context)
{
    if (g_guard_depth > 0)
    {
        size_t top = g_guard_depth - 1;
        g_guard_stack[top].signal_num = signum;
        if (info != nullptr)
        {
            g_guard_stack[top].fault_address = reinterpret_cast<uintptr_t>(info->si_addr);
        }
        siglongjmp(g_guard_stack[top].jmp_buf, 1);
    }

    ForwardSignal(signum, info, context);
}

void InstallSignalHandlers()
{
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = GlobalSignalHandler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, &g_prev_segv);
    sigaction(SIGBUS, &sa, &g_prev_bus);
    sigaction(SIGILL, &sa, &g_prev_ill);
    sigaction(SIGFPE, &sa, &g_prev_fpe);
}

const char* SignalToDescription(int signum)
{
    switch (signum)
    {
    case SIGSEGV:
        return "SIGSEGV (Segmentation violation)";
    case SIGBUS:
        return "SIGBUS (Bus error)";
    case SIGILL:
        return "SIGILL (Illegal instruction)";
    case SIGFPE:
        return "SIGFPE (Floating point exception)";
    default:
        return "Signal fault";
    }
}

} // namespace

GuardedRunResult GuardedRun(const std::function<void()>& fn)
{
    std::call_once(g_handlers_installed, InstallSignalHandlers);

    GuardedRunResult result;

    if (g_guard_depth >= kMaxGuardDepth)
    {
        result.succeeded = false;
        result.description = "Exceeded maximum guard recursion depth";
        return result;
    }

    size_t frame_idx = g_guard_depth++;
    g_guard_stack[frame_idx].signal_num = 0;
    g_guard_stack[frame_idx].fault_address = 0;
    g_guard_stack[frame_idx].is_stop = false;
    g_guard_stack[frame_idx].stop_reason = nullptr;

    if (sigsetjmp(g_guard_stack[frame_idx].jmp_buf, 1) == 0)
    {
        fn();
        --g_guard_depth;
        result.succeeded = true;
    }
    else
    {
        --g_guard_depth;
        result.succeeded = false;
        if (g_guard_stack[frame_idx].is_stop)
        {
            result.stopped = true;
            result.stop_reason = g_guard_stack[frame_idx].stop_reason ? g_guard_stack[frame_idx].stop_reason : "";
            result.description = "Execution stopped: " + result.stop_reason;
        }
        else
        {
            result.native_exception_code = static_cast<uint32_t>(g_guard_stack[frame_idx].signal_num);
            result.fault_address = g_guard_stack[frame_idx].fault_address;
            result.description = SignalToDescription(g_guard_stack[frame_idx].signal_num);
        }
    }

    return result;
}

[[noreturn]] void RaiseStop(const char* reason)
{
    if (g_guard_depth > 0)
    {
        size_t top = g_guard_depth - 1;
        g_guard_stack[top].is_stop = true;
        g_guard_stack[top].stop_reason = reason;
        siglongjmp(g_guard_stack[top].jmp_buf, 2);
    }
    std::abort();
}

} // namespace repsp::platform

extern "C" void repsp_raise_stop(const char* reason)
{
    repsp::platform::RaiseStop(reason);
}
