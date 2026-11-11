#include "repsp/platform/virtual_memory.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace repsp::platform
{

namespace
{

DWORD ToNativeProtection(MemoryProtection protection)
{
    switch (protection)
    {
    case MemoryProtection::kNoAccess:
        return PAGE_NOACCESS;
    case MemoryProtection::kReadOnly:
        return PAGE_READONLY;
    case MemoryProtection::kReadWrite:
        return PAGE_READWRITE;
    case MemoryProtection::kExecuteReadWrite:
        return PAGE_EXECUTE_READWRITE;
    default:
        return PAGE_NOACCESS;
    }
}

} // namespace

void* ReserveAddressSpace(size_t size)
{
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool CommitMemory(void* address, size_t size, MemoryProtection protection)
{
    DWORD native_prot = ToNativeProtection(protection);
    void* ptr = VirtualAlloc(address, size, MEM_COMMIT, native_prot);
    return ptr != nullptr;
}

bool DecommitMemory(void* address, size_t size)
{
    return VirtualFree(address, size, MEM_DECOMMIT) != FALSE;
}

bool ReleaseAddressSpace(void* address, size_t size)
{
    (void)size;
    return VirtualFree(address, 0, MEM_RELEASE) != FALSE;
}

bool ProtectMemory(void* address, size_t size, MemoryProtection protection)
{
    DWORD old_prot = 0;
    DWORD native_prot = ToNativeProtection(protection);
    return VirtualProtect(address, size, native_prot, &old_prot) != FALSE;
}

} // namespace repsp::platform
