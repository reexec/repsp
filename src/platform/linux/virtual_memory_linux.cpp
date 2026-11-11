#include "repsp/platform/virtual_memory.h"

#include <sys/mman.h>
#include <unistd.h>

namespace repsp::platform
{

namespace
{

int ToNativeProtection(MemoryProtection protection)
{
    switch (protection)
    {
    case MemoryProtection::kNoAccess:
        return PROT_NONE;
    case MemoryProtection::kReadOnly:
        return PROT_READ;
    case MemoryProtection::kReadWrite:
        return PROT_READ | PROT_WRITE;
    case MemoryProtection::kExecuteReadWrite:
        return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
        return PROT_NONE;
    }
}

} // namespace

void* ReserveAddressSpace(size_t size)
{
    void* ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;
}

bool CommitMemory(void* address, size_t size, MemoryProtection protection)
{
    int prot = ToNativeProtection(protection);
    return mprotect(address, size, prot) == 0;
}

bool DecommitMemory(void* address, size_t size)
{
    int res1 = madvise(address, size, MADV_DONTNEED);
    int res2 = mprotect(address, size, PROT_NONE);
    return (res1 == 0) && (res2 == 0);
}

bool ReleaseAddressSpace(void* address, size_t size)
{
    return munmap(address, size) == 0;
}

bool ProtectMemory(void* address, size_t size, MemoryProtection protection)
{
    int prot = ToNativeProtection(protection);
    return mprotect(address, size, prot) == 0;
}

} // namespace repsp::platform
