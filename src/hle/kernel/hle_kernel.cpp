#include "repsp/hle/hle_kernel.h"
#include "repsp/hle/kernel.h"
#include "kernel_internal.h"
#include "repsp/runtime/clock.h"
#include "repsp/runtime/kernel_objects.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/user_partition.h"
#include "repsp/runtime/vfs.h"

namespace repsp::hle
{

namespace kernel
{

void ReserveModuleImage(uint32_t base, uint32_t size)
{
    if (base > runtime::UserPartition::kUserMemBase)
    {
        runtime::GetUserPartition().ReserveRange(
            runtime::UserPartition::kUserMemBase,
            base - runtime::UserPartition::kUserMemBase,
            "kernel_reserved_gap");
    }
    if (size > 0)
    {
        runtime::GetUserPartition().ReserveRange(base, size, "module_image");
    }
}

void Register(const KernelConfig& config)
{
    detail::ResetIoFiles();
    detail::ResetThreadManSync();
    detail::ResetModules();
    runtime::GetScheduler().Reset();
    runtime::GetUserPartition().Reset();
    runtime::GetKernelObjects().Reset();
    runtime::ResetBootClock();
    runtime::GetVfs().Clear();

    for (const auto& m : config.mounts)
    {
        runtime::GetVfs().Mount(m.device, m.host_path, m.read_only);
    }

    if (config.module_image_base != 0 || config.module_image_size != 0)
    {
        ReserveModuleImage(config.module_image_base, config.module_image_size);
    }

    detail::RegisterSysMem();
    detail::RegisterThreadManThread();
    detail::RegisterThreadManSync();
    detail::RegisterIoFileMgr();
    detail::RegisterModuleMgr();
    detail::RegisterLoadExec();
    detail::RegisterUtils();
    detail::RegisterStdio();
    detail::RegisterKernelLibrary();
    detail::RegisterPowerSuspend();
    detail::RegisterUmd();
}

std::vector<OpenFileInfo> GetOpenFilesSnapshot()
{
    return detail::SnapshotIoFiles();
}

void PrintKernelSummary()
{
    runtime::GetNidRegistry().PrintCallSummary();

    auto threads = runtime::GetScheduler().GetThreadsSnapshot();
    runtime::Log(runtime::LogLevel::kInfo, "=== Guest Threads (%zu) ===", threads.size());
    for (const auto& t : threads)
    {
        runtime::Log(runtime::LogLevel::kInfo,
                     "  [uid=0x%X] name=\"%s\" prio=%d state=%d wait=%d(%s) entry=0x%08X stack=0x%X",
                     t.uid, t.name.c_str(), t.current_priority, static_cast<int>(t.state),
                     static_cast<int>(t.wait_type), t.wait_detail.c_str(), t.entry, t.stack_size);
    }

    auto files = GetOpenFilesSnapshot();
    runtime::Log(runtime::LogLevel::kInfo, "=== Open Files (%zu) ===", files.size());
    for (const auto& f : files)
    {
        runtime::Log(runtime::LogLevel::kInfo,
                     "  fd=%d path=\"%s\" offset=%llu async=%d",
                     f.fd, f.guest_path.c_str(), static_cast<unsigned long long>(f.offset),
                     f.has_async_result ? 1 : 0);
    }

    auto blocks = runtime::GetUserPartition().GetBlocks();
    runtime::Log(runtime::LogLevel::kInfo, "=== User Partition Blocks (%zu) ===", blocks.size());
    for (const auto& b : blocks)
    {
        runtime::Log(runtime::LogLevel::kInfo,
                     "  [uid=0x%X] 0x%08X..0x%08X (%u bytes) name=\"%s\"%s",
                     b.uid, b.addr, b.addr + b.size, b.size, b.name.c_str(),
                     b.reserved ? " [reserved]" : "");
    }
}

} // namespace kernel

void RegisterHleKernel()
{
    kernel::Register(kernel::KernelConfig{});
}

} // namespace repsp::hle
