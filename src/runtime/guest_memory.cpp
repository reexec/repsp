#include "repsp/runtime/guest_memory.h"
#include "repsp/platform/virtual_memory.h"

#include <cstring>

namespace repsp::runtime
{

GuestMemory::GuestMemory() = default;

GuestMemory::~GuestMemory()
{
    Shutdown();
}

bool GuestMemory::Initialize()
{
    if (base_ != nullptr)
    {
        return true;
    }

    base_ = static_cast<uint8_t*>(platform::ReserveAddressSpace(kWindowSize));
    if (base_ == nullptr)
    {
        return false;
    }

    // Commit valid physical regions
    if (!platform::CommitMemory(base_ + kScratchpadAddress, kScratchpadSize, platform::MemoryProtection::kReadWrite))
    {
        Shutdown();
        return false;
    }

    if (!platform::CommitMemory(base_ + kVramAddress, kVramSize, platform::MemoryProtection::kReadWrite))
    {
        Shutdown();
        return false;
    }

    if (!platform::CommitMemory(base_ + kKernelRamAddress, kKernelRamSize, platform::MemoryProtection::kReadWrite))
    {
        Shutdown();
        return false;
    }

    if (!platform::CommitMemory(base_ + kUserRamAddress, kUserRamSize, platform::MemoryProtection::kReadWrite))
    {
        Shutdown();
        return false;
    }

    return true;
}

void GuestMemory::Shutdown()
{
    if (base_ != nullptr)
    {
        platform::ReleaseAddressSpace(base_, kWindowSize);
        base_ = nullptr;
    }
}

uint8_t* GuestMemory::Translate(uint32_t guest_addr)
{
    uint32_t phys = guest_addr & kFoldMask;
    return base_ + phys;
}

const uint8_t* GuestMemory::Translate(uint32_t guest_addr) const
{
    uint32_t phys = guest_addr & kFoldMask;
    return base_ + phys;
}

GuestMemoryRegion GuestMemory::QueryRegion(uint32_t guest_addr) const
{
    uint32_t phys = guest_addr & kFoldMask;

    if (phys >= kScratchpadAddress && phys < kScratchpadAddress + kScratchpadSize)
    {
        return GuestMemoryRegion::kScratchpad;
    }
    if (phys >= kVramAddress && phys < kVramAddress + kVramSize)
    {
        return GuestMemoryRegion::kVram;
    }
    if (phys >= kKernelRamAddress && phys < kKernelRamAddress + kKernelRamSize)
    {
        return GuestMemoryRegion::kKernelRam;
    }
    if (phys >= kUserRamAddress && phys < kUserRamAddress + kUserRamSize)
    {
        return GuestMemoryRegion::kUserRam;
    }

    return GuestMemoryRegion::kInvalid;
}

bool GuestMemory::IsValidAddress(uint32_t guest_addr, size_t size) const
{
    if (size == 0)
    {
        return true;
    }

    GuestMemoryRegion start_region = QueryRegion(guest_addr);
    if (start_region == GuestMemoryRegion::kInvalid)
    {
        return false;
    }

    uint32_t end_addr = guest_addr + static_cast<uint32_t>(size - 1);
    GuestMemoryRegion end_region = QueryRegion(end_addr);
    return start_region == end_region;
}

uint8_t GuestMemory::ReadU8(uint32_t guest_addr) const
{
    return *Translate(guest_addr);
}

uint16_t GuestMemory::ReadU16(uint32_t guest_addr) const
{
    const uint8_t* p = Translate(guest_addr);
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t GuestMemory::ReadU32(uint32_t guest_addr) const
{
    const uint8_t* p = Translate(guest_addr);
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t GuestMemory::ReadU64(uint32_t guest_addr) const
{
    uint64_t low = ReadU32(guest_addr);
    uint64_t high = ReadU32(guest_addr + 4);
    return low | (high << 32);
}

void GuestMemory::WriteU8(uint32_t guest_addr, uint8_t value)
{
    *Translate(guest_addr) = value;
}

void GuestMemory::WriteU16(uint32_t guest_addr, uint16_t value)
{
    uint8_t* p = Translate(guest_addr);
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void GuestMemory::WriteU32(uint32_t guest_addr, uint32_t value)
{
    uint8_t* p = Translate(guest_addr);
    p[0] = static_cast<uint8_t>(value & 0xFF);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void GuestMemory::WriteU64(uint32_t guest_addr, uint64_t value)
{
    WriteU32(guest_addr, static_cast<uint32_t>(value & 0xFFFFFFFF));
    WriteU32(guest_addr + 4, static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF));
}

void GuestMemory::ReadBlock(uint32_t guest_addr, void* dst, size_t size) const
{
    if (size > 0 && dst != nullptr)
    {
        std::memcpy(dst, Translate(guest_addr), size);
    }
}

void GuestMemory::WriteBlock(uint32_t guest_addr, const void* src, size_t size)
{
    if (size > 0 && src != nullptr)
    {
        std::memcpy(Translate(guest_addr), src, size);
    }
}

GuestMemory& GetGuestMemory()
{
    static GuestMemory instance;
    return instance;
}

} // namespace repsp::runtime
