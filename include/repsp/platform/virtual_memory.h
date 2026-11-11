#ifndef REPSP_PLATFORM_VIRTUAL_MEMORY_H
#define REPSP_PLATFORM_VIRTUAL_MEMORY_H

#include <cstddef>
#include <cstdint>

namespace repsp::platform
{

enum class MemoryProtection
{
    kNoAccess,
    kReadOnly,
    kReadWrite,
    kExecuteReadWrite,
};

void* ReserveAddressSpace(size_t size);
bool CommitMemory(void* address, size_t size, MemoryProtection protection);
bool DecommitMemory(void* address, size_t size);
bool ReleaseAddressSpace(void* address, size_t size);
bool ProtectMemory(void* address, size_t size, MemoryProtection protection);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_VIRTUAL_MEMORY_H
