#include "kernel_internal.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

namespace repsp::hle::kernel::detail
{

namespace
{

void HleCpuSuspendIntr()
{
    uint32_t prev = runtime::GetScheduler().SuspendInterrupts();
    recomp::Return(prev);
}

void HleCpuResumeIntr()
{
    uint32_t flags = recomp::Arg<uint32_t>(0);
    runtime::GetScheduler().ResumeInterrupts(flags);
    recomp::Return();
}

} // namespace

void RegisterKernelLibrary()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("Kernel_Library", "sceKernelCpuSuspendIntr", HleCpuSuspendIntr);
    reg.RegisterByName("Kernel_Library", "sceKernelCpuResumeIntr", HleCpuResumeIntr);
}

} // namespace repsp::hle::kernel::detail
