#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/kernel_objects.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"

#include <mutex>

namespace repsp::hle::kernel::detail
{

namespace
{

// PSP volatile memory: 4 MiB at 0x08400000 (upper half of the 8 MiB kernel RAM window).
// Source: PSPSDK pspsuspend.h / uOFW.
constexpr uint32_t kVolatileMemAddress = 0x08400000u;
constexpr uint32_t kVolatileMemSize = 4u * 1024u * 1024u;

std::mutex g_volatile_mutex;
bool g_volatile_locked = false;

void HlePowerTick()
{
    recomp::Return(0);
}

void HlePowerRegisterCallback()
{
    int32_t slot = recomp::Arg<int32_t>(0);
    runtime::KernelUid cbid = recomp::Arg<runtime::KernelUid>(1);
    runtime::Log(runtime::LogLevel::kDebug, "[Power] scePowerRegisterCallback(slot=%d, cbid=0x%X)", slot, cbid);
    recomp::Return(0);
}

void HleKernelVolatileMemTryLock()
{
    const int32_t unk = recomp::Arg<int32_t>(0);
    const uint32_t ptr_out_addr = recomp::Arg<uint32_t>(1);
    const uint32_t size_out_addr = recomp::Arg<uint32_t>(2);

    if (unk != 0)
    {
        recomp::Return(kSceKernelErrorInvalidValue);
        return;
    }

    auto& mem = runtime::GetGuestMemory();
    if ((ptr_out_addr != 0 && !mem.IsValidAddress(ptr_out_addr, 4u)) ||
        (size_out_addr != 0 && !mem.IsValidAddress(size_out_addr, 4u)))
    {
        recomp::Return(kSceKernelErrorInvalidPointer);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_volatile_mutex);
        if (g_volatile_locked)
        {
            recomp::Return(kScePowerErrorTakenSlot);
            return;
        }
        g_volatile_locked = true;
    }

    if (ptr_out_addr != 0)
    {
        mem.WriteU32(ptr_out_addr, kVolatileMemAddress);
    }
    if (size_out_addr != 0)
    {
        mem.WriteU32(size_out_addr, kVolatileMemSize);
    }
    recomp::Return(0);
}

void HleKernelVolatileMemUnlock()
{
    const int32_t unk = recomp::Arg<int32_t>(0);
    if (unk != 0)
    {
        recomp::Return(kSceKernelErrorInvalidValue);
        return;
    }

    std::lock_guard<std::mutex> lock(g_volatile_mutex);
    if (!g_volatile_locked)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }
    g_volatile_locked = false;
    recomp::Return(0);
}

} // namespace

void ResetPowerSuspend()
{
    std::lock_guard<std::mutex> lock(g_volatile_mutex);
    g_volatile_locked = false;
}

void RegisterPowerSuspend()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceSuspendForUser", "sceKernelPowerTick", HlePowerTick);
    reg.RegisterByName("sceSuspendForUser", "sceKernelVolatileMemTryLock", HleKernelVolatileMemTryLock);
    reg.RegisterByName("sceSuspendForUser", "sceKernelVolatileMemUnlock", HleKernelVolatileMemUnlock);
    reg.RegisterByName("scePower", "scePowerRegisterCallback", HlePowerRegisterCallback);
}

} // namespace repsp::hle::kernel::detail
