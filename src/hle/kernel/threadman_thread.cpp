#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/clock.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <string>

namespace repsp::hle::kernel::detail
{

namespace
{

std::string ReadGuestCString(uint32_t addr, size_t max_len = 64)
{
    if (addr == 0)
    {
        return "";
    }
    const char* p = reinterpret_cast<const char*>(runtime::GetGuestMemory().Translate(addr));
    std::string out;
    for (size_t i = 0; i < max_len && p[i] != '\0'; ++i)
    {
        out.push_back(p[i]);
    }
    return out;
}

void HleCreateThread()
{
    uint32_t name_addr = recomp::Arg<uint32_t>(0);
    uint32_t entry = recomp::Arg<uint32_t>(1);
    int32_t init_priority = recomp::Arg<int32_t>(2);
    uint32_t stack_size = recomp::Arg<uint32_t>(3);
    uint32_t attr = recomp::Arg<uint32_t>(4);
    // arg 5: option (ignored)

    std::string name = ReadGuestCString(name_addr, 31);
    uint32_t current_gp = recomp::GetGpr(28); // $gp at creation time
    int32_t uid = runtime::GetScheduler().CreateThread(name, entry, init_priority, stack_size, attr, current_gp);
    recomp::Return(uid);
}

void HleStartThread()
{
    runtime::KernelUid thid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t arglen = recomp::Arg<uint32_t>(1);
    uint32_t argp = recomp::Arg<uint32_t>(2);

    int32_t res = runtime::GetScheduler().StartThread(thid, arglen, argp);
    recomp::Return(res);
}

void HleExitThread()
{
    int32_t status = recomp::Arg<int32_t>(0);
    runtime::GetScheduler().ExitThread(status, false);
}

void HleDeleteThread()
{
    runtime::KernelUid thid = recomp::Arg<runtime::KernelUid>(0);
    int32_t res = runtime::GetScheduler().DeleteThread(thid);
    recomp::Return(res);
}

void HleTerminateDeleteThread()
{
    runtime::KernelUid thid = recomp::Arg<runtime::KernelUid>(0);
    int32_t res = runtime::GetScheduler().TerminateThread(thid, true);
    recomp::Return(res);
}

void HleWaitThreadEnd()
{
    runtime::KernelUid thid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t timeout_addr = recomp::Arg<uint32_t>(1);

    uint32_t timeout_val = 0;
    uint32_t* timeout_ptr = nullptr;
    if (timeout_addr != 0)
    {
        timeout_val = runtime::GetGuestMemory().ReadU32(timeout_addr);
        timeout_ptr = &timeout_val;
    }

    int32_t res = runtime::GetScheduler().WaitThreadEnd(thid, timeout_ptr, false);
    if (timeout_addr != 0 && res == 0)
    {
        runtime::GetGuestMemory().WriteU32(timeout_addr, 0);
    }
    recomp::Return(res);
}

void HleGetThreadId()
{
    recomp::Return(runtime::GetScheduler().GetCurrentThreadId());
}

void HleChangeThreadPriority()
{
    runtime::KernelUid thid = recomp::Arg<runtime::KernelUid>(0);
    int32_t priority = recomp::Arg<int32_t>(1);
    int32_t res = runtime::GetScheduler().ChangeThreadPriority(thid, priority);
    recomp::Return(res);
}

void HleSleepThread()
{
    recomp::Return(runtime::GetScheduler().SleepThread(false));
}

void HleWakeupThread()
{
    runtime::KernelUid thid = recomp::Arg<runtime::KernelUid>(0);
    recomp::Return(runtime::GetScheduler().WakeupThread(thid));
}

void HleDelayThread()
{
    uint32_t delay_us = recomp::Arg<uint32_t>(0);
    recomp::Return(runtime::GetScheduler().DelayThread(delay_us, false));
}

void HleDelayThreadCB()
{
    uint32_t delay_us = recomp::Arg<uint32_t>(0);
    recomp::Return(runtime::GetScheduler().DelayThread(delay_us, true));
}

void HleCreateCallback()
{
    uint32_t name_addr = recomp::Arg<uint32_t>(0);
    uint32_t func = recomp::Arg<uint32_t>(1);
    uint32_t arg = recomp::Arg<uint32_t>(2);

    std::string name = ReadGuestCString(name_addr, 31);
    int32_t cbid = runtime::GetScheduler().CreateCallback(name, func, arg);
    recomp::Return(cbid);
}

void HleCheckCallback()
{
    recomp::Return(runtime::GetScheduler().CheckCallbacks());
}

void HleGetSystemTimeWide()
{
    recomp::Return(runtime::GetSystemTimeWide());
}

void HleGetSystemTimeLow()
{
    recomp::Return(runtime::GetSystemTimeLow());
}

} // namespace

void RegisterThreadManThread()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("ThreadManForUser", "sceKernelCreateThread", HleCreateThread);
    reg.RegisterByName("ThreadManForUser", "sceKernelStartThread", HleStartThread);
    reg.RegisterByName("ThreadManForUser", "sceKernelExitThread", HleExitThread);
    reg.RegisterByName("ThreadManForUser", "sceKernelDeleteThread", HleDeleteThread);
    reg.RegisterByName("ThreadManForUser", "sceKernelTerminateDeleteThread", HleTerminateDeleteThread);
    reg.RegisterByName("ThreadManForUser", "sceKernelWaitThreadEnd", HleWaitThreadEnd);
    reg.RegisterByName("ThreadManForUser", "sceKernelGetThreadId", HleGetThreadId);
    reg.RegisterByName("ThreadManForUser", "sceKernelChangeThreadPriority", HleChangeThreadPriority);
    reg.RegisterByName("ThreadManForUser", "sceKernelSleepThread", HleSleepThread);
    reg.RegisterByName("ThreadManForUser", "sceKernelWakeupThread", HleWakeupThread);
    reg.RegisterByName("ThreadManForUser", "sceKernelDelayThread", HleDelayThread);
    reg.RegisterByName("ThreadManForUser", "sceKernelDelayThreadCB", HleDelayThreadCB);
    reg.RegisterByName("ThreadManForUser", "sceKernelCreateCallback", HleCreateCallback);
    reg.RegisterByName("ThreadManForUser", "sceKernelCheckCallback", HleCheckCallback);
    reg.RegisterByName("ThreadManForUser", "sceKernelGetSystemTimeWide", HleGetSystemTimeWide);
    reg.RegisterByName("ThreadManForUser", "sceKernelGetSystemTimeLow", HleGetSystemTimeLow);
}

} // namespace repsp::hle::kernel::detail
