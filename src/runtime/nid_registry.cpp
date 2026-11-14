#include "repsp/runtime/nid_registry.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_names.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/sha1.h"

#include <cstdio>
#include <cstdlib>

namespace repsp::runtime
{

NidRegistry::NidRegistry()
{
    const char* env_stop = std::getenv("REPSP_STOP_ON_UNIMPLEMENTED");
    if (env_stop != nullptr && *env_stop != '\0' && *env_stop != '0')
    {
        stop_on_unimplemented_ = true;
    }

    const char* env_trace = std::getenv("REPSP_HLE_TRACE");
    if (env_trace != nullptr && *env_trace != '\0' && *env_trace != '0')
    {
        trace_enabled_ = true;
    }

    const char* env_limit = std::getenv("REPSP_HLE_TRACE_LIMIT");
    if (env_limit != nullptr && *env_limit != '\0')
    {
        trace_limit_ = std::strtoull(env_limit, nullptr, 10);
    }
}

void NidRegistry::Register(const std::string& module_name, uint32_t nid, const std::string& function_name, HleHandler handler)
{
    HleEntry entry;
    entry.module_name = module_name;
    entry.nid = nid;
    entry.function_name = function_name;
    entry.handler = std::move(handler);
    auto [count_it, inserted] = call_counts_.try_emplace(module_name + ":" + function_name, 0);
    (void)inserted;
    entry.call_count_ptr = &count_it->second;
    entries_[nid] = std::move(entry);
}

void NidRegistry::RegisterByName(const std::string& module_name, const std::string& function_name, HleHandler handler)
{
    uint32_t nid = ComputeNid(function_name);
    Register(module_name, nid, function_name, std::move(handler));
}

const HleEntry* NidRegistry::Find(uint32_t nid) const
{
    auto it = entries_.find(nid);
    if (it != entries_.end())
    {
        return &it->second;
    }
    return nullptr;
}

bool NidRegistry::Invoke(uint32_t nid)
{
    // Keep no owning objects (std::function, std::string) on the stack across
    // handler(), because RaiseStop / thread exit may longjmp out of this frame.
    const HleEntry* entry = Find(nid);
    if (entry != nullptr)
    {
        if (entry->call_count_ptr != nullptr)
        {
            ++(*entry->call_count_ptr);
        }
        const bool should_trace = trace_enabled_ && (trace_limit_ == 0 || trace_count_ < trace_limit_);
        uint32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        const auto* ops = GetScheduler().GetCpuOps();
        if (should_trace && ops != nullptr && ops->get_gpr != nullptr)
        {
            a0 = ops->get_gpr(4);
            a1 = ops->get_gpr(5);
            a2 = ops->get_gpr(6);
            a3 = ops->get_gpr(7);
        }

        if (entry->handler)
        {
            entry->handler();
        }

        if (should_trace)
        {
            ++trace_count_;
            uint32_t v0 = (ops != nullptr && ops->get_gpr != nullptr) ? ops->get_gpr(2) : 0;
            Log(LogLevel::kInfo, "[HLE_TRACE] %s:%s(0x%08X, 0x%08X, 0x%08X, 0x%08X) = 0x%08X",
                entry->module_name.c_str(), entry->function_name.c_str(), a0, a1, a2, a3, v0);
        }
        return true;
    }

    uint64_t count = ++unimplemented_counts_[nid];
    if (count == 1)
    {
        const std::string formatted = FormatKnownNid(nid);
        Log(LogLevel::kWarn, "[HLE] Unimplemented NID call: %s", formatted.c_str());
    }

    if (stop_on_unimplemented_)
    {
        const std::string formatted = FormatKnownNid(nid);
        Log(LogLevel::kError, "[HLE] Stopping on unimplemented NID: %s", formatted.c_str());
        static thread_local char reason[192];
        std::snprintf(reason, sizeof(reason), "Unimplemented NID %s", formatted.c_str());
        platform::RaiseStop(reason);
    }

    return false;
}

uint64_t NidRegistry::GetUnimplementedCount(uint32_t nid) const
{
    auto it = unimplemented_counts_.find(nid);
    if (it != unimplemented_counts_.end())
    {
        return it->second;
    }
    return 0;
}

void NidRegistry::PrintUnimplementedSummary() const
{
    if (unimplemented_counts_.empty())
    {
        Log(LogLevel::kInfo, "[HLE] No unimplemented NIDs called.");
        return;
    }

    Log(LogLevel::kInfo, "=== Unimplemented NID Call Summary ===");
    for (const auto& [nid, count] : unimplemented_counts_)
    {
        Log(LogLevel::kInfo, "  NID 0x%08X: %llu calls", nid, static_cast<unsigned long long>(count));
    }
}

void NidRegistry::PrintCallSummary() const
{
    if (call_counts_.empty())
    {
        Log(LogLevel::kInfo, "[HLE] No HLE calls recorded.");
        return;
    }

    Log(LogLevel::kInfo, "=== HLE Call Count Summary ===");
    for (const auto& [name, count] : call_counts_)
    {
        Log(LogLevel::kInfo, "  %s: %llu calls", name.c_str(), static_cast<unsigned long long>(count));
    }
}

NidRegistry& GetNidRegistry()
{
    static NidRegistry instance;
    return instance;
}

} // namespace repsp::runtime
