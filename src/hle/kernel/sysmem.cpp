#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/user_partition.h"

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

void HleAllocPartitionMemory()
{
    int32_t partition_id = recomp::Arg<int32_t>(0);
    uint32_t name_addr = recomp::Arg<uint32_t>(1);
    uint32_t type = recomp::Arg<uint32_t>(2);
    uint32_t size = recomp::Arg<uint32_t>(3);
    uint32_t addr = recomp::Arg<uint32_t>(4);

    if (partition_id != 2)
    {
        runtime::Log(runtime::LogLevel::kWarn, "[SysMem] Refused AllocPartitionMemory for partition %d", partition_id);
        recomp::Return(kSceKernelErrorIllegalPartitionId);
        return;
    }

    std::string name = ReadGuestCString(name_addr, 31);
    int32_t uid = runtime::GetUserPartition().Alloc(
        static_cast<runtime::MemAllocType>(type),
        size,
        addr,
        name);
    recomp::Return(uid);
}

void HleFreePartitionMemory()
{
    runtime::KernelUid uid = recomp::Arg<runtime::KernelUid>(0);
    int32_t res = runtime::GetUserPartition().Free(uid);
    recomp::Return(res);
}

void HleGetBlockHeadAddr()
{
    runtime::KernelUid uid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t addr = 0;
    int32_t res = runtime::GetUserPartition().GetHeadAddr(uid, &addr);
    if (res < 0)
    {
        recomp::Return(res);
        return;
    }
    recomp::Return(addr);
}

void HleMaxFreeMemSize()
{
    recomp::Return(runtime::GetUserPartition().MaxFreeBlockSize());
}

void HleTotalFreeMemSize()
{
    recomp::Return(runtime::GetUserPartition().TotalFreeBytes());
}

// Advisory CRT version-reporting calls (uOFW / psprecomp sysmem.c): record and return 0.
uint32_t g_compiled_sdk_version = 0;
uint32_t g_compiler_version = 0;

void HleSetCompiledSdkVersion()
{
    g_compiled_sdk_version = recomp::Arg<uint32_t>(0);
    recomp::Return(kSceKernelErrorOk);
}

void HleSetCompilerVersion()
{
    g_compiler_version = recomp::Arg<uint32_t>(0);
    recomp::Return(kSceKernelErrorOk);
}

} // namespace

void RegisterSysMem()
{
    g_compiled_sdk_version = 0;
    g_compiler_version = 0;

    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("SysMemUserForUser", "sceKernelAllocPartitionMemory", HleAllocPartitionMemory);
    reg.RegisterByName("SysMemUserForUser", "sceKernelFreePartitionMemory", HleFreePartitionMemory);
    reg.RegisterByName("SysMemUserForUser", "sceKernelGetBlockHeadAddr", HleGetBlockHeadAddr);
    reg.RegisterByName("SysMemUserForUser", "sceKernelMaxFreeMemSize", HleMaxFreeMemSize);
    reg.RegisterByName("SysMemUserForUser", "sceKernelTotalFreeMemSize", HleTotalFreeMemSize);
    reg.RegisterByName("SysMemUserForUser", "sceKernelSetCompiledSdkVersion", HleSetCompiledSdkVersion);
    reg.RegisterByName("SysMemUserForUser", "sceKernelSetCompilerVersion", HleSetCompilerVersion);
}

} // namespace repsp::hle::kernel::detail
