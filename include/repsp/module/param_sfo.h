#ifndef REPSP_MODULE_PARAM_SFO_H
#define REPSP_MODULE_PARAM_SFO_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace repsp::module
{

enum class SfoValueType
{
    kUtf8String,
    kInteger,
    kBinary,
};

struct SfoEntry
{
    SfoValueType type;
    std::variant<std::string, uint32_t, std::vector<uint8_t>> value;
};

class ParamSfo
{
public:
    static std::optional<ParamSfo> Parse(const uint8_t* data, size_t size);

    bool HasKey(const std::string& key) const;
    std::optional<std::string> GetString(const std::string& key) const;
    std::optional<uint32_t> GetInteger(const std::string& key) const;
    const std::map<std::string, SfoEntry>& GetEntries() const { return entries_; }

private:
    std::map<std::string, SfoEntry> entries_;
};

} // namespace repsp::module

#endif // REPSP_MODULE_PARAM_SFO_H
