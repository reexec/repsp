#include "kernel_internal.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"

namespace repsp::hle::kernel::detail
{

namespace
{

uint32_t g_gpo_value = 0;

void HleDcacheWritebackAll()
{
    recomp::Return();
}

void HleDcacheWritebackInvalidateAll()
{
    recomp::Return();
}

void HleGetGpi()
{
    recomp::Return(0u);
}

void HleSetGpo()
{
    uint32_t val = recomp::Arg<uint32_t>(0);
    g_gpo_value = val;
    runtime::Log(runtime::LogLevel::kDebug, "[Utils] sceKernelSetGPO(0x%08X)", val);
    recomp::Return();
}

} // namespace

void RegisterUtils()
{
    g_gpo_value = 0;
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("UtilsForUser", "sceKernelDcacheWritebackAll", HleDcacheWritebackAll);
    reg.RegisterByName("UtilsForUser", "sceKernelDcacheWritebackInvalidateAll", HleDcacheWritebackInvalidateAll);
    reg.RegisterByName("UtilsForUser", "sceKernelGetGPI", HleGetGpi);
    reg.RegisterByName("UtilsForUser", "sceKernelSetGPO", HleSetGpo);
}

} // namespace repsp::hle::kernel::detail
