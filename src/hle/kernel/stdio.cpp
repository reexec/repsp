#include "kernel_internal.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/nid_registry.h"

namespace repsp::hle::kernel::detail
{

namespace
{

void HleStdin()
{
    recomp::Return(0);
}

void HleStdout()
{
    recomp::Return(1);
}

void HleStderr()
{
    recomp::Return(2);
}

} // namespace

void RegisterStdio()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("StdioForUser", "sceKernelStdin", HleStdin);
    reg.RegisterByName("StdioForUser", "sceKernelStdout", HleStdout);
    reg.RegisterByName("StdioForUser", "sceKernelStderr", HleStderr);
}

} // namespace repsp::hle::kernel::detail
