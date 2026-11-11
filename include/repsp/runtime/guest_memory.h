#ifndef REPSP_RUNTIME_GUEST_MEMORY_H
#define REPSP_RUNTIME_GUEST_MEMORY_H

#include <cstddef>
#include <cstdint>

namespace repsp::runtime
{

enum class GuestMemoryRegion
{
    kInvalid,
    kScratchpad,
    kVram,
    kKernelRam,
    kUserRam,
};

class GuestMemory
{
public:
    static constexpr uint32_t kFoldMask = 0x1FFFFFFF;
    static constexpr size_t kWindowSize = 0x0A000000; // 160 MiB

    static constexpr uint32_t kScratchpadAddress = 0x00010000;
    static constexpr size_t kScratchpadSize = 16 * 1024; // 16 KiB

    static constexpr uint32_t kVramAddress = 0x04000000;
    static constexpr size_t kVramSize = 2 * 1024 * 1024; // 2 MiB

    static constexpr uint32_t kKernelRamAddress = 0x08000000;
    static constexpr size_t kKernelRamSize = 8 * 1024 * 1024; // 8 MiB

    static constexpr uint32_t kUserRamAddress = 0x08800000;
    static constexpr size_t kUserRamSize = 24 * 1024 * 1024; // 24 MiB

    GuestMemory();
    ~GuestMemory();

    GuestMemory(const GuestMemory&) = delete;
    GuestMemory& operator=(const GuestMemory&) = delete;

    bool Initialize();
    void Shutdown();

    uint8_t* Base() { return base_; }
    const uint8_t* Base() const { return base_; }

    uint8_t* Translate(uint32_t guest_addr);
    const uint8_t* Translate(uint32_t guest_addr) const;

    GuestMemoryRegion QueryRegion(uint32_t guest_addr) const;
    bool IsValidAddress(uint32_t guest_addr, size_t size = 1) const;

    uint8_t ReadU8(uint32_t guest_addr) const;
    uint16_t ReadU16(uint32_t guest_addr) const;
    uint32_t ReadU32(uint32_t guest_addr) const;
    uint64_t ReadU64(uint32_t guest_addr) const;

    void WriteU8(uint32_t guest_addr, uint8_t value);
    void WriteU16(uint32_t guest_addr, uint16_t value);
    void WriteU32(uint32_t guest_addr, uint32_t value);
    void WriteU64(uint32_t guest_addr, uint64_t value);

    void ReadBlock(uint32_t guest_addr, void* dst, size_t size) const;
    void WriteBlock(uint32_t guest_addr, const void* src, size_t size);

private:
    uint8_t* base_ = nullptr;
};

// Global accessor for runtime guest memory instance
GuestMemory& GetGuestMemory();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_GUEST_MEMORY_H
