#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/log.h"
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
}

void NidRegistry::Register(const std::string& module_name, uint32_t nid, const std::string& function_name, HleHandler handler)
{
    HleEntry entry;
    entry.module_name = module_name;
    entry.nid = nid;
    entry.function_name = function_name;
    entry.handler = std::move(handler);
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
    auto it = entries_.find(nid);
    if (it != entries_.end())
    {
        if (it->second.handler)
        {
            it->second.handler();
        }
        return true;
    }

    uint64_t count = ++unimplemented_counts_[nid];
    if (count == 1)
    {
        Log(LogLevel::kWarn, "[HLE] Unimplemented NID call: 0x%08X", nid);
    }

    if (stop_on_unimplemented_)
    {
        Log(LogLevel::kError, "[HLE] Stopping on unimplemented NID: 0x%08X", nid);
        std::abort();
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

NidRegistry& GetNidRegistry()
{
    static NidRegistry instance;
    return instance;
}

} // namespace repsp::runtime
