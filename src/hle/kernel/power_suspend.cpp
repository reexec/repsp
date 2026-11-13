#include "kernel_internal.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"

namespace repsp::hle::kernel::detail
{

namespace
{

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

} // namespace

void RegisterPowerSuspend()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceSuspendForUser", "sceKernelPowerTick", HlePowerTick);
    reg.RegisterByName("scePower", "scePowerRegisterCallback", HlePowerRegisterCallback);
}

} // namespace repsp::hle::kernel::detail
