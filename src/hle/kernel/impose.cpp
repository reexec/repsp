#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"

namespace repsp::hle::kernel::detail
{

namespace
{

int32_t g_impose_language = 1;
int32_t g_impose_button = 0;
int32_t g_impose_umd_popup = 1;

// PSPSDK pspimpose_driver.h / uOFW impose: record overlay language and button mode, return 0.
void HleImposeSetLanguageMode()
{
    g_impose_language = recomp::Arg<int32_t>(0);
    g_impose_button = recomp::Arg<int32_t>(1);
    runtime::Log(runtime::LogLevel::kDebug, "[Impose] sceImposeSetLanguageMode(lang=%d, button=%d)",
                 g_impose_language, g_impose_button);
    recomp::Return(kSceKernelErrorOk);
}

// PSPSDK pspimpose_driver.h / uOFW impose: record UMD popup enable flag, return 0.
void HleImposeSetUMDPopup()
{
    g_impose_umd_popup = recomp::Arg<int32_t>(0);
    runtime::Log(runtime::LogLevel::kDebug, "[Impose] sceImposeSetUMDPopup(mode=%d)",
                 g_impose_umd_popup);
    recomp::Return(kSceKernelErrorOk);
}

} // namespace

void RegisterImpose()
{
    g_impose_language = 1;
    g_impose_button = 0;
    g_impose_umd_popup = 1;

    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceImpose", "sceImposeSetLanguageMode", HleImposeSetLanguageMode);
    reg.RegisterByName("sceImpose", "sceImposeSetUMDPopup", HleImposeSetUMDPopup);
}

} // namespace repsp::hle::kernel::detail
