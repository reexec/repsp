#ifndef REPSP_HLE_KERNEL_H
#define REPSP_HLE_KERNEL_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace repsp::hle::kernel
{

struct KernelMount
{
    std::string device;
    std::filesystem::path host_path;
    bool read_only = false;
};

struct KernelConfig
{
    std::vector<KernelMount> mounts;

    // Loaded main module image range in user partition (0x08800000..0x0A000000)
    uint32_t module_image_base = 0;
    uint32_t module_image_size = 0;
    uint32_t module_gp = 0;
    uint32_t entry_address = 0;

    // Initial module_start thread parameters
    std::string first_thread_name = "user_main";
    int32_t first_thread_priority = 0x20;
    uint32_t first_thread_stack_size = 0x40000;
    uint32_t first_thread_attr = 0;
};

struct OpenFileInfo
{
    int32_t fd = -1;
    std::string guest_path;
    uint64_t offset = 0;
    bool has_async_result = false;
};

// Register all 65 kernel/UMD HLE functions, configure VFS mounts, and reserve the module image range.
void Register(const KernelConfig& config);

// Reserve a module image range in the user partition (also reserves pre-base gap from 0x08800000).
void ReserveModuleImage(uint32_t base, uint32_t size);

// Diagnostics / Trace summary helpers
std::vector<OpenFileInfo> GetOpenFilesSnapshot();
void PrintKernelSummary();

} // namespace repsp::hle::kernel

#endif // REPSP_HLE_KERNEL_H
