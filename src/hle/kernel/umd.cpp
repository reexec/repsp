#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

namespace repsp::hle::kernel::detail
{

namespace
{

// PSP_UMD_PRESENT (0x02) | PSP_UMD_INITED (0x10) | PSP_UMD_READY (0x20)
constexpr uint32_t kUmdReadyStat = 0x32u;

void HleUmdActivate()
{
    recomp::Return(0);
}

void HleUmdDeactivate()
{
    recomp::Return(0);
}

void HleUmdGetDriveStat()
{
    recomp::Return(kUmdReadyStat);
}

void HleUmdWaitDriveStat()
{
    uint32_t stat = recomp::Arg<uint32_t>(0);
    if ((kUmdReadyStat & stat) != 0)
    {
        recomp::Return(0);
        return;
    }
    int32_t res = runtime::GetScheduler().WaitCurrent(runtime::WaitType::kUmd, 0, "sceUmdWaitDriveStat", nullptr, false);
    recomp::Return(res);
}

void HleUmdWaitDriveStatWithTimer()
{
    uint32_t stat = recomp::Arg<uint32_t>(0);
    uint32_t timeout = recomp::Arg<uint32_t>(1);
    if ((kUmdReadyStat & stat) != 0)
    {
        recomp::Return(0);
        return;
    }
    int32_t res = runtime::GetScheduler().WaitCurrent(runtime::WaitType::kUmd, 0, "sceUmdWaitDriveStatWithTimer", &timeout, false);
    recomp::Return(res);
}

void HleUmdCancelWaitDriveStat()
{
    runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kUmd, 0, kSceKernelErrorWaitCancel);
    recomp::Return(0);
}

} // namespace

void RegisterUmd()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceUmdUser", "sceUmdActivate", HleUmdActivate);
    reg.RegisterByName("sceUmdUser", "sceUmdDeactivate", HleUmdDeactivate);
    reg.RegisterByName("sceUmdUser", "sceUmdGetDriveStat", HleUmdGetDriveStat);
    reg.RegisterByName("sceUmdUser", "sceUmdWaitDriveStat", HleUmdWaitDriveStat);
    reg.RegisterByName("sceUmdUser", "sceUmdWaitDriveStatWithTimer", HleUmdWaitDriveStatWithTimer);
    reg.RegisterByName("sceUmdUser", "sceUmdCancelWaitDriveStat", HleUmdCancelWaitDriveStat);
}

} // namespace repsp::hle::kernel::detail
