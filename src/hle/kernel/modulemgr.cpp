#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/kernel_objects.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/vfs.h"

#include <filesystem>
#include <string>

namespace repsp::hle::kernel::detail
{

namespace
{

struct HleModuleObject
{
    runtime::KernelUid uid = 0;
    std::string path;
    std::string name;
};

std::string ReadGuestCString(uint32_t addr, size_t max_len = 512)
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

void HleLoadModule()
{
    uint32_t path_addr = recomp::Arg<uint32_t>(0);
    std::string path = ReadGuestCString(path_addr);

    std::filesystem::path host_path;
    std::string normalized;
    auto st = runtime::GetVfs().Resolve(path, false, &host_path, &normalized);
    if (st != runtime::VfsResolveStatus::kOk || !std::filesystem::exists(host_path))
    {
        runtime::Log(runtime::LogLevel::kWarn, "[ModuleMgr] LoadModule missing: %s", path.c_str());
        recomp::Return(kSceKernelErrorErrnoEnoent);
        return;
    }

    auto mod = std::make_shared<HleModuleObject>();
    mod->path = normalized;
    mod->name = host_path.filename().string();
    mod->uid = runtime::GetKernelObjects().Create(runtime::KernelObjectType::kModule, mod);
    runtime::Log(runtime::LogLevel::kInfo, "[ModuleMgr] HLE module loaded: %s (uid=0x%X)", mod->name.c_str(), mod->uid);
    recomp::Return(mod->uid);
}

void HleStartModule()
{
    runtime::KernelUid modid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t status_addr = recomp::Arg<uint32_t>(3);

    std::shared_ptr<HleModuleObject> mod;
    if (runtime::GetKernelObjects().LookupAs(modid, runtime::KernelObjectType::kModule, &mod) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }
    if (status_addr != 0)
    {
        runtime::GetGuestMemory().WriteU32(status_addr, 0);
    }
    recomp::Return(0);
}

void HleStopModule()
{
    runtime::KernelUid modid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t status_addr = recomp::Arg<uint32_t>(3);

    std::shared_ptr<HleModuleObject> mod;
    if (runtime::GetKernelObjects().LookupAs(modid, runtime::KernelObjectType::kModule, &mod) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }
    if (status_addr != 0)
    {
        runtime::GetGuestMemory().WriteU32(status_addr, 0);
    }
    recomp::Return(0);
}

void HleUnloadModule()
{
    runtime::KernelUid modid = recomp::Arg<runtime::KernelUid>(0);
    if (runtime::GetKernelObjects().Remove(modid, runtime::KernelObjectType::kModule) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }
    recomp::Return(0);
}

void HleSelfStopUnloadModule()
{
    platform::RaiseStop("module self-unload");
}

} // namespace

void ResetModules()
{
    // Managed by KernelObjectTable.
}

void RegisterModuleMgr()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("ModuleMgrForUser", "sceKernelLoadModule", HleLoadModule);
    reg.RegisterByName("ModuleMgrForUser", "sceKernelStartModule", HleStartModule);
    reg.RegisterByName("ModuleMgrForUser", "sceKernelStopModule", HleStopModule);
    reg.RegisterByName("ModuleMgrForUser", "sceKernelUnloadModule", HleUnloadModule);
    reg.RegisterByName("ModuleMgrForUser", "sceKernelSelfStopUnloadModule", HleSelfStopUnloadModule);
}

} // namespace repsp::hle::kernel::detail
