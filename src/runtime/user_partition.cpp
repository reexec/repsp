#include "repsp/runtime/user_partition.h"

#include <algorithm>

namespace repsp::runtime
{

namespace
{

constexpr int32_t kErrNoMemory = static_cast<int32_t>(0x800200D9u);      // SCE_KERNEL_ERROR_NO_MEMORY (uOFW)
constexpr int32_t kErrIllegalType = static_cast<int32_t>(0x800200D7u);   // SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCKTYPE (uOFW)
constexpr int32_t kErrIllegalAddr = static_cast<int32_t>(0x800200D6u);   // SCE_KERNEL_ERROR_ILLEGAL_ADDR (uOFW)
constexpr int32_t kErrIllegalSize = static_cast<int32_t>(0x800200D8u);   // SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE (uOFW)
constexpr int32_t kErrUnknownMbid = static_cast<int32_t>(0x800200CFu);   // SCE_KERNEL_ERROR_UNKNOWN_MBID (uOFW)

uint32_t AlignUp(uint32_t value, uint32_t align)
{
    return (value + align - 1u) & ~(align - 1u);
}

uint32_t AlignDown(uint32_t value, uint32_t align)
{
    return value & ~(align - 1u);
}

bool IsPowerOfTwo(uint32_t v)
{
    return v != 0 && (v & (v - 1u)) == 0;
}

} // namespace

void UserPartition::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& b : blocks_)
    {
        GetKernelObjects().Remove(b.uid, KernelObjectType::kPartitionBlock);
    }
    blocks_.clear();
}

bool UserPartition::OverlapsLocked(uint32_t addr, uint32_t size) const
{
    const uint64_t a_start = addr;
    const uint64_t a_end = a_start + size;
    for (const auto& b : blocks_)
    {
        const uint64_t b_start = b.addr;
        const uint64_t b_end = b_start + b.size;
        if (a_start < b_end && b_start < a_end)
        {
            return true;
        }
    }
    return false;
}

bool UserPartition::PlaceLowLocked(uint32_t size, uint32_t align, uint32_t* out_addr) const
{
    uint32_t candidate = AlignUp(kUserMemBase, align);
    while (candidate <= kUserMemEnd && size <= kUserMemEnd - candidate)
    {
        if (!OverlapsLocked(candidate, size))
        {
            *out_addr = candidate;
            return true;
        }
        uint32_t next = kUserMemEnd;
        for (const auto& b : blocks_)
        {
            const uint64_t b_end = static_cast<uint64_t>(b.addr) + b.size;
            if (b.addr < static_cast<uint64_t>(candidate) + size && b_end > candidate)
            {
                if (b_end < next)
                {
                    next = static_cast<uint32_t>(b_end);
                }
            }
        }
        if (next <= candidate)
        {
            break;
        }
        candidate = AlignUp(next, align);
    }
    return false;
}

bool UserPartition::PlaceHighLocked(uint32_t size, uint32_t align, uint32_t* out_addr) const
{
    if (size > kUserMemSize)
    {
        return false;
    }
    uint32_t candidate = AlignDown(kUserMemEnd - size, align);
    while (candidate >= kUserMemBase)
    {
        if (!OverlapsLocked(candidate, size))
        {
            *out_addr = candidate;
            return true;
        }
        uint32_t prev_block_start = 0;
        bool found_blocking = false;
        for (const auto& b : blocks_)
        {
            const uint64_t b_end = static_cast<uint64_t>(b.addr) + b.size;
            if (b.addr < static_cast<uint64_t>(candidate) + size && b_end > candidate)
            {
                if (!found_blocking || b.addr > prev_block_start)
                {
                    prev_block_start = b.addr;
                    found_blocking = true;
                }
            }
        }
        if (!found_blocking || prev_block_start < kUserMemBase + size)
        {
            break;
        }
        candidate = AlignDown(prev_block_start - size, align);
    }
    return false;
}

KernelUid UserPartition::ReserveRange(uint32_t addr, uint32_t size, const std::string& name)
{
    if (size == 0)
    {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto info = std::make_shared<PartitionBlockInfo>();
    info->addr = addr;
    info->size = size;
    info->name = name;
    info->reserved = true;
    info->uid = GetKernelObjects().Create(KernelObjectType::kPartitionBlock, info);
    blocks_.push_back(*info);
    return info->uid;
}

int32_t UserPartition::Alloc(MemAllocType type, uint32_t size, uint32_t addr_or_align, const std::string& name, uint32_t* out_addr)
{
    if (size == 0)
    {
        return kErrIllegalSize;
    }

    const uint32_t aligned_size = AlignUp(size, 256u);
    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t chosen_addr = 0;
    switch (type)
    {
    case MemAllocType::kLow:
        if (!PlaceLowLocked(aligned_size, 256u, &chosen_addr))
        {
            return kErrNoMemory;
        }
        break;
    case MemAllocType::kHigh:
        if (!PlaceHighLocked(aligned_size, 256u, &chosen_addr))
        {
            return kErrNoMemory;
        }
        break;
    case MemAllocType::kAddr:
        if (addr_or_align < kUserMemBase || addr_or_align >= kUserMemEnd ||
            aligned_size > kUserMemEnd - addr_or_align)
        {
            return kErrIllegalAddr;
        }
        if (OverlapsLocked(addr_or_align, aligned_size))
        {
            return kErrNoMemory;
        }
        chosen_addr = addr_or_align;
        break;
    case MemAllocType::kLowAligned:
    {
        uint32_t align = (addr_or_align < 256u) ? 256u : addr_or_align;
        if (!IsPowerOfTwo(align))
        {
            return kErrIllegalAddr;
        }
        if (!PlaceLowLocked(aligned_size, align, &chosen_addr))
        {
            return kErrNoMemory;
        }
        break;
    }
    case MemAllocType::kHighAligned:
    {
        uint32_t align = (addr_or_align < 256u) ? 256u : addr_or_align;
        if (!IsPowerOfTwo(align))
        {
            return kErrIllegalAddr;
        }
        if (!PlaceHighLocked(aligned_size, align, &chosen_addr))
        {
            return kErrNoMemory;
        }
        break;
    }
    default:
        return kErrIllegalType;
    }

    auto info = std::make_shared<PartitionBlockInfo>();
    info->addr = chosen_addr;
    info->size = aligned_size;
    info->name = name;
    info->reserved = false;
    info->uid = GetKernelObjects().Create(KernelObjectType::kPartitionBlock, info);
    blocks_.push_back(*info);

    if (out_addr != nullptr)
    {
        *out_addr = chosen_addr;
    }
    return info->uid;
}

int32_t UserPartition::Free(KernelUid uid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = blocks_.begin(); it != blocks_.end(); ++it)
    {
        if (it->uid == uid)
        {
            if (it->reserved)
            {
                return kErrUnknownMbid;
            }
            GetKernelObjects().Remove(uid, KernelObjectType::kPartitionBlock);
            blocks_.erase(it);
            return 0;
        }
    }
    return kErrUnknownMbid;
}

int32_t UserPartition::GetHeadAddr(KernelUid uid, uint32_t* out_addr) const
{
    std::shared_ptr<PartitionBlockInfo> block;
    LookupStatus st = GetKernelObjects().LookupAs(uid, KernelObjectType::kPartitionBlock, &block);
    if (st != LookupStatus::kOk || !block)
    {
        return kErrUnknownMbid;
    }
    if (out_addr != nullptr)
    {
        *out_addr = block->addr;
    }
    return 0;
}

uint32_t UserPartition::TotalFreeBytes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t used = 0;
    for (const auto& b : blocks_)
    {
        used += b.size;
    }
    if (used >= kUserMemSize)
    {
        return 0;
    }
    return static_cast<uint32_t>(kUserMemSize - used);
}

uint32_t UserPartition::MaxFreeBlockSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    ranges.reserve(blocks_.size());
    for (const auto& b : blocks_)
    {
        uint32_t s = std::max(b.addr, kUserMemBase);
        uint32_t e = std::min(b.addr + b.size, kUserMemEnd);
        if (s < e)
        {
            ranges.emplace_back(s, e);
        }
    }
    std::sort(ranges.begin(), ranges.end());

    uint32_t cursor = kUserMemBase;
    uint32_t max_gap = 0;
    for (const auto& r : ranges)
    {
        if (r.first > cursor)
        {
            max_gap = std::max(max_gap, r.first - cursor);
        }
        cursor = std::max(cursor, r.second);
    }
    if (kUserMemEnd > cursor)
    {
        max_gap = std::max(max_gap, kUserMemEnd - cursor);
    }
    return max_gap;
}

std::vector<PartitionBlockInfo> UserPartition::GetBlocks() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_;
}

UserPartition& GetUserPartition()
{
    static UserPartition instance;
    return instance;
}

} // namespace repsp::runtime
