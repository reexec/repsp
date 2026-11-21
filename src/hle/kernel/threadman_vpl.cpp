#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/kernel_objects.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/user_partition.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace repsp::hle::kernel::detail
{

namespace
{

// PSPSDK pspthreadman.h / uOFW threadman VPL attributes.
constexpr uint32_t kPspVplAttrWaitPrio = 0x0100u;
constexpr uint32_t kPspVplAttrExt = 0x4000u; // Allocate backing pool from high memory
constexpr uint32_t kVplAlign = 8u;

struct FreeSpan
{
    uint32_t addr = 0;
    uint32_t size = 0;
};

struct VplWaiter
{
    runtime::KernelUid thread_uid = 0;
    int32_t priority = 0x20;
    uint32_t aligned_size = 0;
    uint32_t out_data_addr = 0;
};

struct VplObject
{
    std::mutex mutex;
    runtime::KernelUid uid = 0;
    std::string name;
    uint32_t attr = 0;
    runtime::KernelUid mem_block_uid = 0;
    uint32_t pool_base = 0;
    uint32_t pool_size = 0;
    std::vector<FreeSpan> free_spans;
    std::unordered_map<uint32_t, uint32_t> alloc_sizes;
    std::vector<VplWaiter> waiters;
};

uint32_t AlignUpVpl(uint32_t size)
{
    return (size + (kVplAlign - 1u)) & ~(kVplAlign - 1u);
}

std::string ReadGuestCString(uint32_t addr, size_t max_len = 32)
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

void InsertVplWaiter(std::vector<VplWaiter>& queue, const VplWaiter& item, bool priority_order)
{
    if (!priority_order)
    {
        queue.push_back(item);
        return;
    }
    auto it = std::find_if(queue.begin(), queue.end(), [&](const VplWaiter& existing)
    {
        return item.priority < existing.priority;
    });
    queue.insert(it, item);
}

void RemoveVplWaiter(std::vector<VplWaiter>& queue, runtime::KernelUid thread_uid)
{
    queue.erase(
        std::remove_if(queue.begin(), queue.end(), [&](const VplWaiter& w)
        {
            return w.thread_uid == thread_uid;
        }),
        queue.end());
}

bool TryAllocSpanLocked(VplObject& vpl, uint32_t aligned_size, uint32_t* out_addr)
{
    for (auto it = vpl.free_spans.begin(); it != vpl.free_spans.end(); ++it)
    {
        if (it->size >= aligned_size)
        {
            uint32_t addr = it->addr;
            if (it->size == aligned_size)
            {
                vpl.free_spans.erase(it);
            }
            else
            {
                it->addr += aligned_size;
                it->size -= aligned_size;
            }
            vpl.alloc_sizes[addr] = aligned_size;
            *out_addr = addr;
            return true;
        }
    }
    return false;
}

void ReturnSpanLocked(VplObject& vpl, uint32_t addr, uint32_t size)
{
    auto it = std::lower_bound(
        vpl.free_spans.begin(),
        vpl.free_spans.end(),
        addr,
        [](const FreeSpan& span, uint32_t a) { return span.addr < a; });

    it = vpl.free_spans.insert(it, FreeSpan{addr, size});

    // Coalesce with next span
    auto next = it + 1;
    if (next != vpl.free_spans.end() && it->addr + it->size == next->addr)
    {
        it->size += next->size;
        vpl.free_spans.erase(next);
    }

    // Coalesce with previous span
    if (it != vpl.free_spans.begin())
    {
        auto prev = it - 1;
        if (prev->addr + prev->size == it->addr)
        {
            prev->size += it->size;
            vpl.free_spans.erase(it);
        }
    }
}

uint32_t TotalFreeLocked(const VplObject& vpl)
{
    uint32_t total = 0;
    for (const auto& span : vpl.free_spans)
    {
        total += span.size;
    }
    return total;
}

void HleCreateVpl()
{
    uint32_t name_addr = recomp::Arg<uint32_t>(0);
    int32_t part = recomp::Arg<int32_t>(1);
    uint32_t attr = recomp::Arg<uint32_t>(2);
    uint32_t size = recomp::Arg<uint32_t>(3);

    if (part != 2)
    {
        recomp::Return(kSceKernelErrorIllegalPartitionId);
        return;
    }
    if (size == 0)
    {
        recomp::Return(kSceKernelErrorIllegalMemSize);
        return;
    }

    const uint32_t aligned_pool_size = AlignUpVpl(size);
    if (aligned_pool_size < size)
    {
        recomp::Return(kSceKernelErrorIllegalMemSize);
        return;
    }

    std::string name = ReadGuestCString(name_addr, 31);
    const auto alloc_type = ((attr & kPspVplAttrExt) != 0)
        ? runtime::MemAllocType::kHigh
        : runtime::MemAllocType::kLow;

    uint32_t pool_addr = 0;
    int32_t block_uid = runtime::GetUserPartition().Alloc(
        alloc_type,
        aligned_pool_size,
        0,
        "vpl:" + name,
        &pool_addr);
    if (block_uid < 0)
    {
        recomp::Return(block_uid);
        return;
    }

    auto vpl = std::make_shared<VplObject>();
    vpl->name = std::move(name);
    vpl->attr = attr;
    vpl->mem_block_uid = block_uid;
    vpl->pool_base = pool_addr;
    vpl->pool_size = aligned_pool_size;
    vpl->free_spans.push_back(FreeSpan{pool_addr, aligned_pool_size});
    vpl->uid = runtime::GetKernelObjects().Create(runtime::KernelObjectType::kVpl, vpl);
    recomp::Return(vpl->uid);
}

void HleDeleteVpl()
{
    runtime::KernelUid vplid = recomp::Arg<runtime::KernelUid>(0);
    std::shared_ptr<VplObject> vpl;
    if (runtime::GetKernelObjects().LookupAs(vplid, runtime::KernelObjectType::kVpl, &vpl) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownVplid);
        return;
    }

    runtime::GetKernelObjects().Remove(vplid, runtime::KernelObjectType::kVpl);
    std::vector<runtime::KernelUid> to_wake;
    runtime::KernelUid block_uid = 0;
    {
        std::lock_guard<std::mutex> lock(vpl->mutex);
        to_wake.reserve(vpl->waiters.size());
        for (const auto& w : vpl->waiters)
        {
            to_wake.push_back(w.thread_uid);
        }
        vpl->waiters.clear();
        block_uid = vpl->mem_block_uid;
        vpl->mem_block_uid = 0;
    }
    if (block_uid > 0)
    {
        runtime::GetUserPartition().Free(block_uid);
    }
    runtime::GetScheduler().WakeThreadsFromWait(to_wake, kSceKernelErrorWaitDelete);
    recomp::Return(kSceKernelErrorOk);
}

void DoAllocateVpl(bool allow_callbacks)
{
    runtime::KernelUid vplid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t size = recomp::Arg<uint32_t>(1);
    uint32_t out_data_addr = recomp::Arg<uint32_t>(2);
    uint32_t timeout_addr = recomp::Arg<uint32_t>(3);

    std::shared_ptr<VplObject> vpl;
    if (runtime::GetKernelObjects().LookupAs(vplid, runtime::KernelObjectType::kVpl, &vpl) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownVplid);
        return;
    }
    if (out_data_addr == 0)
    {
        recomp::Return(kSceKernelErrorIllegalAddr);
        return;
    }
    if (size == 0)
    {
        recomp::Return(kSceKernelErrorIllegalMemSize);
        return;
    }

    const uint32_t aligned_size = AlignUpVpl(size);
    if (aligned_size < size || aligned_size > vpl->pool_size)
    {
        recomp::Return(kSceKernelErrorIllegalMemSize);
        return;
    }

    uint32_t timeout_val = 0;
    uint32_t* timeout_ptr = nullptr;
    if (timeout_addr != 0)
    {
        timeout_val = runtime::GetGuestMemory().ReadU32(timeout_addr);
        timeout_ptr = &timeout_val;
    }

    auto& sched = runtime::GetScheduler();
    const runtime::KernelUid cur_uid = sched.GetCurrentThreadId();
    const int32_t cur_prio = sched.GetThreadPriority(cur_uid);

    std::string vpl_name;
    {
        std::lock_guard<std::mutex> lock(vpl->mutex);
        uint32_t alloc_addr = 0;
        if (vpl->waiters.empty() && TryAllocSpanLocked(*vpl, aligned_size, &alloc_addr))
        {
            runtime::GetGuestMemory().WriteU32(out_data_addr, alloc_addr);
            if (timeout_addr != 0)
            {
                runtime::GetGuestMemory().WriteU32(timeout_addr, 0);
            }
            recomp::Return(kSceKernelErrorOk);
            return;
        }
        if (timeout_ptr != nullptr && *timeout_ptr == 0)
        {
            recomp::Return(kSceKernelErrorWaitTimeout);
            return;
        }
        const bool prio_order = (vpl->attr & kPspVplAttrWaitPrio) != 0;
        InsertVplWaiter(vpl->waiters, VplWaiter{cur_uid, cur_prio, aligned_size, out_data_addr}, prio_order);
        vpl_name = vpl->name;
    }

    // Release mutex before WaitCurrent so no lock is held across RaiseStop.
    int32_t wait_res = sched.WaitCurrent(
        runtime::WaitType::kCustom,
        vplid,
        "AllocateVpl:" + vpl_name,
        timeout_ptr,
        allow_callbacks);

    if (wait_res != 0)
    {
        std::lock_guard<std::mutex> lock(vpl->mutex);
        RemoveVplWaiter(vpl->waiters, cur_uid);
        recomp::Return(wait_res);
        return;
    }

    if (timeout_addr != 0)
    {
        runtime::GetGuestMemory().WriteU32(timeout_addr, 0);
    }
    recomp::Return(kSceKernelErrorOk);
}

void HleAllocateVpl()
{
    DoAllocateVpl(false);
}

void HleAllocateVplCB()
{
    DoAllocateVpl(true);
}

void HleTryAllocateVpl()
{
    runtime::KernelUid vplid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t size = recomp::Arg<uint32_t>(1);
    uint32_t out_data_addr = recomp::Arg<uint32_t>(2);

    std::shared_ptr<VplObject> vpl;
    if (runtime::GetKernelObjects().LookupAs(vplid, runtime::KernelObjectType::kVpl, &vpl) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownVplid);
        return;
    }
    if (out_data_addr == 0)
    {
        recomp::Return(kSceKernelErrorIllegalAddr);
        return;
    }
    if (size == 0)
    {
        recomp::Return(kSceKernelErrorIllegalMemSize);
        return;
    }

    const uint32_t aligned_size = AlignUpVpl(size);
    if (aligned_size < size || aligned_size > vpl->pool_size)
    {
        recomp::Return(kSceKernelErrorIllegalMemSize);
        return;
    }

    std::lock_guard<std::mutex> lock(vpl->mutex);
    uint32_t alloc_addr = 0;
    if (!vpl->waiters.empty() || !TryAllocSpanLocked(*vpl, aligned_size, &alloc_addr))
    {
        recomp::Return(kSceKernelErrorNoMemory);
        return;
    }

    runtime::GetGuestMemory().WriteU32(out_data_addr, alloc_addr);
    recomp::Return(kSceKernelErrorOk);
}

void HleFreeVpl()
{
    runtime::KernelUid vplid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t data_addr = recomp::Arg<uint32_t>(1);

    std::shared_ptr<VplObject> vpl;
    if (runtime::GetKernelObjects().LookupAs(vplid, runtime::KernelObjectType::kVpl, &vpl) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownVplid);
        return;
    }

    std::vector<runtime::KernelUid> to_wake;
    {
        std::lock_guard<std::mutex> lock(vpl->mutex);
        auto it = vpl->alloc_sizes.find(data_addr);
        if (it == vpl->alloc_sizes.end())
        {
            recomp::Return(kSceKernelErrorIllegalAddr);
            return;
        }
        const uint32_t freed_size = it->second;
        vpl->alloc_sizes.erase(it);
        ReturnSpanLocked(*vpl, data_addr, freed_size);

        // Handoff in wait-queue order (stop at head if it cannot be satisfied yet).
        while (!vpl->waiters.empty())
        {
            const VplWaiter& head = vpl->waiters.front();
            uint32_t alloc_addr = 0;
            if (!TryAllocSpanLocked(*vpl, head.aligned_size, &alloc_addr))
            {
                break;
            }
            runtime::GetGuestMemory().WriteU32(head.out_data_addr, alloc_addr);
            to_wake.push_back(head.thread_uid);
            vpl->waiters.erase(vpl->waiters.begin());
        }
    }

    runtime::GetScheduler().WakeThreadsFromWait(to_wake, 0);
    recomp::Return(kSceKernelErrorOk);
}

void HleReferVplStatus()
{
    runtime::KernelUid vplid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t info_addr = recomp::Arg<uint32_t>(1);

    std::shared_ptr<VplObject> vpl;
    if (runtime::GetKernelObjects().LookupAs(vplid, runtime::KernelObjectType::kVpl, &vpl) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownVplid);
        return;
    }
    if (info_addr == 0)
    {
        recomp::Return(kSceKernelErrorIllegalAddr);
        return;
    }

    std::lock_guard<std::mutex> lock(vpl->mutex);
    auto& mem = runtime::GetGuestMemory();
    constexpr uint32_t kInfoSize = 56u;
    uint8_t* dst = mem.Translate(info_addr);
    std::memset(dst, 0, kInfoSize);
    mem.WriteU32(info_addr + 0x00u, kInfoSize);
    std::memcpy(dst + 0x04u, vpl->name.c_str(), std::min<size_t>(vpl->name.size(), 31u));
    mem.WriteU32(info_addr + 0x24u, vpl->attr);
    mem.WriteU32(info_addr + 0x28u, vpl->pool_size);
    mem.WriteU32(info_addr + 0x2Cu, TotalFreeLocked(*vpl));
    mem.WriteU32(info_addr + 0x30u, static_cast<uint32_t>(vpl->waiters.size()));
    recomp::Return(kSceKernelErrorOk);
}

} // namespace

void RegisterThreadManVpl()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("ThreadManForUser", "sceKernelCreateVpl", HleCreateVpl);
    reg.RegisterByName("ThreadManForUser", "sceKernelDeleteVpl", HleDeleteVpl);
    reg.RegisterByName("ThreadManForUser", "sceKernelAllocateVpl", HleAllocateVpl);
    reg.RegisterByName("ThreadManForUser", "sceKernelAllocateVplCB", HleAllocateVplCB);
    reg.RegisterByName("ThreadManForUser", "sceKernelTryAllocateVpl", HleTryAllocateVpl);
    reg.RegisterByName("ThreadManForUser", "sceKernelFreeVpl", HleFreeVpl);
    reg.RegisterByName("ThreadManForUser", "sceKernelReferVplStatus", HleReferVplStatus);
}

} // namespace repsp::hle::kernel::detail
