#include "kernel_internal.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/kernel_objects.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"

namespace repsp::hle::kernel::detail
{

namespace
{

runtime::KernelUid g_exit_callback_uid = 0;

void HleExitGame()
{
    platform::RaiseStop("ExitGame");
}

void HleRegisterExitCallback()
{
    runtime::KernelUid cbid = recomp::Arg<runtime::KernelUid>(0);
    g_exit_callback_uid = cbid;
    runtime::Log(runtime::LogLevel::kDebug, "[LoadExec] Registered exit callback: 0x%X", cbid);
    recomp::Return(0);
}

} // namespace

void RegisterLoadExec()
{
    g_exit_callback_uid = 0;
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("LoadExecForUser", "sceKernelExitGame", HleExitGame);
    reg.RegisterByName("LoadExecForUser", "sceKernelRegisterExitCallback", HleRegisterExitCallback);
}

} // namespace repsp::hle::kernel::detail
