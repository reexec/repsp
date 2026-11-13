#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/kernel_objects.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace repsp::hle::kernel::detail
{

namespace
{

// Event flag wait mode flags (PSPSDK pspthreadman.h / uITRON).
constexpr uint32_t kPspEventWaitOr = 0x01u;
constexpr uint32_t kPspEventWaitClearAll = 0x10u;
constexpr uint32_t kPspEventWaitClear = 0x20u;

// Kernel object wait-queue attributes (PSPSDK pspthreadman.h / uOFW threadman).
// 0x100: priority-ordered wait queue (PSP_SEMA_ATTR_PRIO / PSP_EVENT_WAITPRIO); 0x000 is FIFO.
// 0x200: allow multiple waiting threads on an event flag (PSP_EVENT_WAITMULTIPLE).
constexpr uint32_t kPspAttrWaitPrio = 0x100u;
constexpr uint32_t kPspEventAttrWaitMultiple = 0x200u;

struct SemaWaiter
{
    runtime::KernelUid thread_uid = 0;
    int32_t priority = 0x20;
    int32_t need_count = 0;
};

struct EventFlagWaiter
{
    runtime::KernelUid thread_uid = 0;
    int32_t priority = 0x20;
    uint32_t bits = 0;
    uint32_t wait_mode = 0;
    uint32_t out_bits_addr = 0;
};

struct SemaObject
{
    std::mutex mutex;
    runtime::KernelUid uid = 0;
    std::string name;
    uint32_t attr = 0;
    int32_t count = 0;
    int32_t max_count = 0;
    std::vector<SemaWaiter> waiters;
};

struct EventFlagObject
{
    std::mutex mutex;
    runtime::KernelUid uid = 0;
    std::string name;
    uint32_t attr = 0;
    uint32_t init_pattern = 0;
    uint32_t current_pattern = 0;
    std::vector<EventFlagWaiter> waiters;
};

template <typename WaiterT>
void InsertWaiter(std::vector<WaiterT>& queue, const WaiterT& item, bool priority_order)
{
    if (!priority_order)
    {
        queue.push_back(item);
        return;
    }
    auto it = std::find_if(queue.begin(), queue.end(), [&](const WaiterT& existing)
    {
        return item.priority < existing.priority;
    });
    queue.insert(it, item);
}

template <typename WaiterT>
void RemoveWaiterByThread(std::vector<WaiterT>& queue, runtime::KernelUid thread_uid)
{
    queue.erase(
        std::remove_if(queue.begin(), queue.end(), [&](const WaiterT& w)
        {
            return w.thread_uid == thread_uid;
        }),
        queue.end());
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

bool CheckEventFlagCondition(uint32_t pattern, uint32_t bits, uint32_t wait_mode)
{
    if (bits == 0)
    {
        return false;
    }
    if ((wait_mode & kPspEventWaitOr) != 0)
    {
        return (pattern & bits) != 0;
    }
    return (pattern & bits) == bits;
}

void ApplyEventFlagClear(uint32_t* pattern, uint32_t bits, uint32_t wait_mode)
{
    if ((wait_mode & kPspEventWaitClearAll) != 0)
    {
        *pattern = 0;
    }
    else if ((wait_mode & kPspEventWaitClear) != 0)
    {
        *pattern &= ~bits;
    }
}

void HleCreateSema()
{
    uint32_t name_addr = recomp::Arg<uint32_t>(0);
    uint32_t attr = recomp::Arg<uint32_t>(1);
    int32_t init_val = recomp::Arg<int32_t>(2);
    int32_t max_val = recomp::Arg<int32_t>(3);

    if (max_val <= 0 || init_val < 0 || init_val > max_val)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }

    auto sema = std::make_shared<SemaObject>();
    sema->name = ReadGuestCString(name_addr, 31);
    sema->attr = attr;
    sema->count = init_val;
    sema->max_count = max_val;
    sema->uid = runtime::GetKernelObjects().Create(runtime::KernelObjectType::kSema, sema);
    recomp::Return(sema->uid);
}

void HleDeleteSema()
{
    runtime::KernelUid semaid = recomp::Arg<runtime::KernelUid>(0);
    std::shared_ptr<SemaObject> sema;
    if (runtime::GetKernelObjects().LookupAs(semaid, runtime::KernelObjectType::kSema, &sema) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownSemid);
        return;
    }

    runtime::GetKernelObjects().Remove(semaid, runtime::KernelObjectType::kSema);
    std::vector<runtime::KernelUid> to_wake;
    {
        std::lock_guard<std::mutex> lock(sema->mutex);
        to_wake.reserve(sema->waiters.size());
        for (const auto& w : sema->waiters)
        {
            to_wake.push_back(w.thread_uid);
        }
        sema->waiters.clear();
    }
    runtime::GetScheduler().WakeThreadsFromWait(to_wake, kSceKernelErrorWaitDelete);
    recomp::Return(0);
}

void HleWaitSema()
{
    runtime::KernelUid semaid = recomp::Arg<runtime::KernelUid>(0);
    int32_t signal = recomp::Arg<int32_t>(1);
    uint32_t timeout_addr = recomp::Arg<uint32_t>(2);

    if (signal <= 0)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }

    std::shared_ptr<SemaObject> sema;
    if (runtime::GetKernelObjects().LookupAs(semaid, runtime::KernelObjectType::kSema, &sema) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownSemid);
        return;
    }
    if (signal > sema->max_count)
    {
        recomp::Return(kSceKernelErrorError);
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

    std::string sema_name;
    {
        std::lock_guard<std::mutex> lock(sema->mutex);
        if (sema->waiters.empty() && sema->count >= signal)
        {
            sema->count -= signal;
            if (timeout_addr != 0)
            {
                runtime::GetGuestMemory().WriteU32(timeout_addr, 0);
            }
            recomp::Return(0);
            return;
        }
        if (timeout_ptr != nullptr && *timeout_ptr == 0)
        {
            recomp::Return(kSceKernelErrorWaitTimeout);
            return;
        }
        const bool prio_order = (sema->attr & kPspAttrWaitPrio) != 0;
        InsertWaiter(sema->waiters, SemaWaiter{cur_uid, cur_prio, signal}, prio_order);
        sema_name = sema->name;
    }

    int32_t wait_res = sched.WaitCurrent(
        runtime::WaitType::kSema,
        semaid,
        "WaitSema:" + sema_name,
        timeout_ptr,
        false);

    if (wait_res != 0)
    {
        std::lock_guard<std::mutex> lock(sema->mutex);
        RemoveWaiterByThread(sema->waiters, cur_uid);
        recomp::Return(wait_res);
        return;
    }

    if (timeout_addr != 0)
    {
        runtime::GetGuestMemory().WriteU32(timeout_addr, 0);
    }
    recomp::Return(0);
}

void HleSignalSema()
{
    runtime::KernelUid semaid = recomp::Arg<runtime::KernelUid>(0);
    int32_t signal = recomp::Arg<int32_t>(1);

    if (signal < 0)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }

    std::shared_ptr<SemaObject> sema;
    if (runtime::GetKernelObjects().LookupAs(semaid, runtime::KernelObjectType::kSema, &sema) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownSemid);
        return;
    }

    std::vector<runtime::KernelUid> to_wake;
    {
        std::lock_guard<std::mutex> lock(sema->mutex);
        if (sema->count + signal > sema->max_count)
        {
            recomp::Return(kSceKernelErrorSemaOvf);
            return;
        }
        sema->count += signal;

        // Handoff at signal time in wait-queue order (stop at head if count < need_count).
        while (!sema->waiters.empty())
        {
            const SemaWaiter& head = sema->waiters.front();
            if (sema->count < head.need_count)
            {
                break;
            }
            sema->count -= head.need_count;
            to_wake.push_back(head.thread_uid);
            sema->waiters.erase(sema->waiters.begin());
        }
    }

    runtime::GetScheduler().WakeThreadsFromWait(to_wake, 0);
    recomp::Return(0);
}

void HleCreateEventFlag()
{
    uint32_t name_addr = recomp::Arg<uint32_t>(0);
    uint32_t attr = recomp::Arg<uint32_t>(1);
    uint32_t bits = recomp::Arg<uint32_t>(2);

    auto evf = std::make_shared<EventFlagObject>();
    evf->name = ReadGuestCString(name_addr, 31);
    evf->attr = attr;
    evf->init_pattern = bits;
    evf->current_pattern = bits;
    evf->uid = runtime::GetKernelObjects().Create(runtime::KernelObjectType::kEventFlag, evf);
    recomp::Return(evf->uid);
}

void HleDeleteEventFlag()
{
    runtime::KernelUid evfid = recomp::Arg<runtime::KernelUid>(0);
    std::shared_ptr<EventFlagObject> evf;
    if (runtime::GetKernelObjects().LookupAs(evfid, runtime::KernelObjectType::kEventFlag, &evf) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownEvfid);
        return;
    }

    runtime::GetKernelObjects().Remove(evfid, runtime::KernelObjectType::kEventFlag);
    std::vector<runtime::KernelUid> to_wake;
    {
        std::lock_guard<std::mutex> lock(evf->mutex);
        to_wake.reserve(evf->waiters.size());
        for (const auto& w : evf->waiters)
        {
            to_wake.push_back(w.thread_uid);
        }
        evf->waiters.clear();
    }
    runtime::GetScheduler().WakeThreadsFromWait(to_wake, kSceKernelErrorWaitDelete);
    recomp::Return(0);
}

void HleSetEventFlag()
{
    runtime::KernelUid evfid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t bits = recomp::Arg<uint32_t>(1);

    std::shared_ptr<EventFlagObject> evf;
    if (runtime::GetKernelObjects().LookupAs(evfid, runtime::KernelObjectType::kEventFlag, &evf) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownEvfid);
        return;
    }

    std::vector<runtime::KernelUid> to_wake;
    {
        std::lock_guard<std::mutex> lock(evf->mutex);
        evf->current_pattern |= bits;

        // Handoff at set time in wait-queue order: write out_bits and apply clear options per satisfied waiter.
        for (auto it = evf->waiters.begin(); it != evf->waiters.end(); )
        {
            if (CheckEventFlagCondition(evf->current_pattern, it->bits, it->wait_mode))
            {
                if (it->out_bits_addr != 0)
                {
                    runtime::GetGuestMemory().WriteU32(it->out_bits_addr, evf->current_pattern);
                }
                ApplyEventFlagClear(&evf->current_pattern, it->bits, it->wait_mode);
                to_wake.push_back(it->thread_uid);
                it = evf->waiters.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    runtime::GetScheduler().WakeThreadsFromWait(to_wake, 0);
    recomp::Return(0);
}

void HleClearEventFlag()
{
    runtime::KernelUid evfid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t bits = recomp::Arg<uint32_t>(1);

    std::shared_ptr<EventFlagObject> evf;
    if (runtime::GetKernelObjects().LookupAs(evfid, runtime::KernelObjectType::kEventFlag, &evf) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownEvfid);
        return;
    }

    std::lock_guard<std::mutex> lock(evf->mutex);
    // PSP sceKernelClearEventFlag keeps bits that are 1 in the mask (pattern &= bits).
    evf->current_pattern &= bits;
    recomp::Return(0);
}

void HleWaitEventFlag()
{
    runtime::KernelUid evfid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t bits = recomp::Arg<uint32_t>(1);
    uint32_t wait_mode = recomp::Arg<uint32_t>(2);
    uint32_t out_bits_addr = recomp::Arg<uint32_t>(3);
    uint32_t timeout_addr = recomp::Arg<uint32_t>(4);

    if (bits == 0)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }

    std::shared_ptr<EventFlagObject> evf;
    if (runtime::GetKernelObjects().LookupAs(evfid, runtime::KernelObjectType::kEventFlag, &evf) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownEvfid);
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

    std::string evf_name;
    {
        std::lock_guard<std::mutex> lock(evf->mutex);
        if ((evf->attr & kPspEventAttrWaitMultiple) == 0 && !evf->waiters.empty())
        {
            // Without PSP_EVENT_WAITMULTIPLE (0x200), a second concurrent waiter is rejected.
            recomp::Return(kSceKernelErrorError);
            return;
        }
        if (CheckEventFlagCondition(evf->current_pattern, bits, wait_mode))
        {
            if (out_bits_addr != 0)
            {
                runtime::GetGuestMemory().WriteU32(out_bits_addr, evf->current_pattern);
            }
            ApplyEventFlagClear(&evf->current_pattern, bits, wait_mode);
            if (timeout_addr != 0)
            {
                runtime::GetGuestMemory().WriteU32(timeout_addr, 0);
            }
            recomp::Return(0);
            return;
        }
        if (timeout_ptr != nullptr && *timeout_ptr == 0)
        {
            if (out_bits_addr != 0)
            {
                runtime::GetGuestMemory().WriteU32(out_bits_addr, evf->current_pattern);
            }
            recomp::Return(kSceKernelErrorWaitTimeout);
            return;
        }
        const bool prio_order = (evf->attr & kPspAttrWaitPrio) != 0;
        InsertWaiter(evf->waiters, EventFlagWaiter{cur_uid, cur_prio, bits, wait_mode, out_bits_addr}, prio_order);
        evf_name = evf->name;
    }

    int32_t wait_res = sched.WaitCurrent(
        runtime::WaitType::kEventFlag,
        evfid,
        "WaitEventFlag:" + evf_name,
        timeout_ptr,
        false);

    if (wait_res != 0)
    {
        std::lock_guard<std::mutex> lock(evf->mutex);
        RemoveWaiterByThread(evf->waiters, cur_uid);
        if (out_bits_addr != 0)
        {
            runtime::GetGuestMemory().WriteU32(out_bits_addr, evf->current_pattern);
        }
        recomp::Return(wait_res);
        return;
    }

    if (timeout_addr != 0)
    {
        runtime::GetGuestMemory().WriteU32(timeout_addr, 0);
    }
    recomp::Return(0);
}

void HlePollEventFlag()
{
    runtime::KernelUid evfid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t bits = recomp::Arg<uint32_t>(1);
    uint32_t wait_mode = recomp::Arg<uint32_t>(2);
    uint32_t out_bits_addr = recomp::Arg<uint32_t>(3);

    if (bits == 0)
    {
        recomp::Return(kSceKernelErrorError);
        return;
    }

    std::shared_ptr<EventFlagObject> evf;
    if (runtime::GetKernelObjects().LookupAs(evfid, runtime::KernelObjectType::kEventFlag, &evf) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownEvfid);
        return;
    }

    std::lock_guard<std::mutex> lock(evf->mutex);
    if (out_bits_addr != 0)
    {
        runtime::GetGuestMemory().WriteU32(out_bits_addr, evf->current_pattern);
    }
    if (!CheckEventFlagCondition(evf->current_pattern, bits, wait_mode))
    {
        recomp::Return(kSceKernelErrorEvfCond);
        return;
    }

    ApplyEventFlagClear(&evf->current_pattern, bits, wait_mode);
    recomp::Return(0);
}

void HleReferEventFlagStatus()
{
    runtime::KernelUid evfid = recomp::Arg<runtime::KernelUid>(0);
    uint32_t status_addr = recomp::Arg<uint32_t>(1);

    std::shared_ptr<EventFlagObject> evf;
    if (runtime::GetKernelObjects().LookupAs(evfid, runtime::KernelObjectType::kEventFlag, &evf) != runtime::LookupStatus::kOk)
    {
        recomp::Return(kSceKernelErrorUnknownEvfid);
        return;
    }

    if (status_addr == 0)
    {
        recomp::Return(kSceKernelErrorIllegalAddr);
        return;
    }

    std::lock_guard<std::mutex> lock(evf->mutex);
    uint8_t* dst = runtime::GetGuestMemory().Translate(status_addr);
    uint32_t info_size = runtime::GetGuestMemory().ReadU32(status_addr);
    if (info_size == 0 || info_size > 56)
    {
        info_size = 56;
    }
    std::memset(dst, 0, info_size);
    runtime::GetGuestMemory().WriteU32(status_addr, info_size);
    if (info_size >= 36)
    {
        size_t copy_len = std::min<size_t>(evf->name.size(), 31);
        std::memcpy(dst + 4, evf->name.data(), copy_len);
    }
    if (info_size >= 40)
    {
        runtime::GetGuestMemory().WriteU32(status_addr + 36, evf->attr);
    }
    if (info_size >= 44)
    {
        runtime::GetGuestMemory().WriteU32(status_addr + 40, evf->init_pattern);
    }
    if (info_size >= 48)
    {
        runtime::GetGuestMemory().WriteU32(status_addr + 44, evf->current_pattern);
    }
    if (info_size >= 52)
    {
        runtime::GetGuestMemory().WriteU32(status_addr + 48, static_cast<uint32_t>(evf->waiters.size()));
    }

    recomp::Return(0);
}

} // namespace

void ResetThreadManSync()
{
    // Objects are owned by KernelObjectTable and cleared on Reset.
}

void RegisterThreadManSync()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("ThreadManForUser", "sceKernelCreateSema", HleCreateSema);
    reg.RegisterByName("ThreadManForUser", "sceKernelDeleteSema", HleDeleteSema);
    reg.RegisterByName("ThreadManForUser", "sceKernelWaitSema", HleWaitSema);
    reg.RegisterByName("ThreadManForUser", "sceKernelSignalSema", HleSignalSema);
    reg.RegisterByName("ThreadManForUser", "sceKernelCreateEventFlag", HleCreateEventFlag);
    reg.RegisterByName("ThreadManForUser", "sceKernelDeleteEventFlag", HleDeleteEventFlag);
    reg.RegisterByName("ThreadManForUser", "sceKernelSetEventFlag", HleSetEventFlag);
    reg.RegisterByName("ThreadManForUser", "sceKernelClearEventFlag", HleClearEventFlag);
    reg.RegisterByName("ThreadManForUser", "sceKernelWaitEventFlag", HleWaitEventFlag);
    reg.RegisterByName("ThreadManForUser", "sceKernelPollEventFlag", HlePollEventFlag);
    reg.RegisterByName("ThreadManForUser", "sceKernelReferEventFlagStatus", HleReferEventFlagStatus);
}

} // namespace repsp::hle::kernel::detail
