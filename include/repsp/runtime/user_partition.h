#ifndef REPSP_RUNTIME_USER_PARTITION_H
#define REPSP_RUNTIME_USER_PARTITION_H

#include "repsp/runtime/kernel_objects.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace repsp::runtime
{

enum class MemAllocType : uint32_t
{
    kLow = 0,
    kHigh = 1,
    kAddr = 2,
    kLowAligned = 3,
    kHighAligned = 4,
};

struct PartitionBlockInfo
{
    KernelUid uid = 0;
    uint32_t addr = 0;
    uint32_t size = 0;
    std::string name;
    bool reserved = false;
};

class UserPartition
{
public:
    static constexpr uint32_t kUserMemBase = 0x08800000u;
    static constexpr uint32_t kUserMemSize = 24u * 1024u * 1024u; // 24 MiB (PSP-1000)
    static constexpr uint32_t kUserMemEnd = kUserMemBase + kUserMemSize;

    void Reset();

    // Reserve an existing range (e.g. kernel/user gap and loaded module image).
    KernelUid ReserveRange(uint32_t addr, uint32_t size, const std::string& name);

    // Allocate a block in the user partition. Returns >= 0 (UID) on success, or negative error code on failure.
    int32_t Alloc(MemAllocType type, uint32_t size, uint32_t addr_or_align, const std::string& name, uint32_t* out_addr = nullptr);

    int32_t Free(KernelUid uid);
    int32_t GetHeadAddr(KernelUid uid, uint32_t* out_addr) const;
    uint32_t TotalFreeBytes() const;
    uint32_t MaxFreeBlockSize() const;

    std::vector<PartitionBlockInfo> GetBlocks() const;

private:
    bool OverlapsLocked(uint32_t addr, uint32_t size) const;
    bool PlaceLowLocked(uint32_t size, uint32_t align, uint32_t* out_addr) const;
    bool PlaceHighLocked(uint32_t size, uint32_t align, uint32_t* out_addr) const;

    mutable std::mutex mutex_;
    std::vector<PartitionBlockInfo> blocks_;
};

UserPartition& GetUserPartition();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_USER_PARTITION_H
