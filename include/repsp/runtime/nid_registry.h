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

    void PrintUnimplementedSummary() const;

private:
    std::unordered_map<uint32_t, HleEntry> entries_;
    std::map<uint32_t, uint64_t> unimplemented_counts_;
    bool stop_on_unimplemented_ = false;
};

NidRegistry& GetNidRegistry();

} // namespace repsp::runtime

#endif // REPSP_RUNTIME_NID_REGISTRY_H
