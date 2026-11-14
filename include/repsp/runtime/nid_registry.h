#ifndef REPSP_RUNTIME_NID_REGISTRY_H
#define REPSP_RUNTIME_NID_REGISTRY_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

namespace repsp::runtime
{

constexpr uint32_t kSceKernelErrorNotImplemented = 0x80020002;

using HleHandler = std::function<void()>;

struct HleEntry
{
    std::string module_name;
    uint32_t nid = 0;
    std::string function_name;
    HleHandler handler;
    uint64_t* call_count_ptr = nullptr; // Stable pointer into call_counts_ node (built at Register time)
};

class NidRegistry
{
public:
    NidRegistry();

    void Register(const std::string& module_name, uint32_t nid, const std::string& function_name, HleHandler handler);
    void RegisterByName(const std::string& module_name, const std::string& function_name, HleHandler handler);

    const HleEntry* Find(uint32_t nid) const;
    bool Invoke(uint32_t nid);

    uint64_t GetUnimplementedCount(uint32_t nid) const;
    const std::map<uint32_t, uint64_t>& GetUnimplementedCounts() const { return unimplemented_counts_; }

    void SetStopOnUnimplemented(bool stop) { stop_on_unimplemented_ = stop; }
    bool GetStopOnUnimplemented() const { return stop_on_unimplemented_; }

    void SetTraceEnabled(bool enabled) { trace_enabled_ = enabled; }
    bool IsTraceEnabled() const { return trace_enabled_; }
    void SetTraceLimit(uint64_t limit) { trace_limit_ = limit; }
    const std::map<std::string, uint64_t>& GetCallCounts() const { return call_counts_; }

    void PrintUnimplementedSummary() const;
    void PrintCallSummary() const;

private:
    std::unordered_map<uint32_t, HleEntry> entries_;
    std::map<uint32_t, uint64_t> unimplemented_counts_;
    std::map<std::string, uint64_t> call_counts_;
    bool stop_on_unimplemented_ = false;
    bool trace_enabled_ = false;
    uint64_t trace_count_ = 0;
    uint64_t trace_limit_ = 0;
};

NidRegistry& GetNidRegistry();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_NID_REGISTRY_H
