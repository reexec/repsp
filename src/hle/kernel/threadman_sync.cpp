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

namespace repsp::hle::kernel::detail
{

namespace
{

constexpr uint32_t kPspEventWaitOr = 0x01u;
constexpr uint32_t kPspEventWaitClearAll = 0x10u;
constexpr uint32_t kPspEventWaitClear = 0x20u;

struct SemaObject
{
    std::mutex mutex;
    runtime::KernelUid uid = 0;
    std::string name;
    uint32_t attr = 0;
    int32_t count = 0;
    int32_t max_count = 0;
    int32_t num_wait_threads = 0;
};

struct EventFlagObject
{
    std::mutex mutex;
    runtime::KernelUid uid = 0;
    std::string name;
    uint32_t attr = 0;
    uint32_t init_pattern = 0;
    uint32_t current_pattern = 0;
    int32_t num_wait_threads = 0;
};

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
    runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kSema, semaid, kSceKernelErrorWaitDelete);
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

    uint32_t timeout_val = 0;
    uint32_t* timeout_ptr = nullptr;
    if (timeout_addr != 0)
    {
        timeout_val = runtime::GetGuestMemory().ReadU32(timeout_addr);
        timeout_ptr = &timeout_val;
    }

    while (true)
    {
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

        std::string sema_name;
        {
            std::lock_guard<std::mutex> lock(sema->mutex);
            if (sema->count >= signal)
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
            ++sema->num_wait_threads;
            sema_name = sema->name;
        }

        int32_t wait_res = runtime::GetScheduler().WaitCurrent(
            runtime::WaitType::kSema,
            semaid,
            "WaitSema:" + sema_name,
            timeout_ptr,
            false);

        {
            std::lock_guard<std::mutex> lock(sema->mutex);
            if (sema->num_wait_threads > 0)
            {
                --sema->num_wait_threads;
            }
        }

        if (wait_res != 0)
        {
            recomp::Return(wait_res);
            return;
        }
    }
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

    {
        std::lock_guard<std::mutex> lock(sema->mutex);
        if (sema->count + signal > sema->max_count)
        {
            recomp::Return(kSceKernelErrorSemaOvf);
            return;
        }
        sema->count += signal;
    }

    runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kSema, semaid, 0);
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
    runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kEventFlag, evfid, kSceKernelErrorWaitDelete);
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

    {
        std::lock_guard<std::mutex> lock(evf->mutex);
        evf->current_pattern |= bits;
    }

    runtime::GetScheduler().WakeAllWaiters(runtime::WaitType::kEventFlag, evfid, 0);
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

    uint32_t timeout_val = 0;
    uint32_t* timeout_ptr = nullptr;
    if (timeout_addr != 0)
    {
        timeout_val = runtime::GetGuestMemory().ReadU32(timeout_addr);
        timeout_ptr = &timeout_val;
    }

    while (true)
    {
        std::shared_ptr<EventFlagObject> evf;
        if (runtime::GetKernelObjects().LookupAs(evfid, runtime::KernelObjectType::kEventFlag, &evf) != runtime::LookupStatus::kOk)
        {
            recomp::Return(kSceKernelErrorUnknownEvfid);
            return;
        }

        std::string evf_name;
        {
            std::lock_guard<std::mutex> lock(evf->mutex);
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
            ++evf->num_wait_threads;
            evf_name = evf->name;
        }

        int32_t wait_res = runtime::GetScheduler().WaitCurrent(
            runtime::WaitType::kEventFlag,
            evfid,
            "WaitEventFlag:" + evf_name,
            timeout_ptr,
            false);

        {
            std::lock_guard<std::mutex> lock(evf->mutex);
            if (evf->num_wait_threads > 0)
            {
                --evf->num_wait_threads;
            }
        }

        if (wait_res != 0)
        {
            if (out_bits_addr != 0)
            {
                std::lock_guard<std::mutex> lock(evf->mutex);
                runtime::GetGuestMemory().WriteU32(out_bits_addr, evf->current_pattern);
            }
            recomp::Return(wait_res);
            return;
        }
    }
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
        runtime::GetGuestMemory().WriteU32(status_addr + 48, static_cast<uint32_t>(evf->num_wait_threads));
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
