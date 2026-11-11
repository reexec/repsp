#include "repsp/module/param_sfo.h"

#include <cstring>

namespace repsp::module
{

namespace
{

uint16_t ReadU16Le(const uint8_t* ptr)
{
    return static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
}

uint32_t ReadU32Le(const uint8_t* ptr)
{
    return static_cast<uint32_t>(ptr[0]) |
           (static_cast<uint32_t>(ptr[1]) << 8) |
           (static_cast<uint32_t>(ptr[2]) << 16) |
           (static_cast<uint32_t>(ptr[3]) << 24);
}

constexpr uint32_t kSfoMagic = 0x46535000; // "\0PSF" in little-endian
constexpr size_t kHeaderSize = 20;
constexpr size_t kEntrySize = 16;

constexpr uint16_t kFormatUtf8Special = 0x0004;
constexpr uint16_t kFormatUtf8String  = 0x0204;
constexpr uint16_t kFormatInteger     = 0x0404;

} // namespace

std::optional<ParamSfo> ParamSfo::Parse(const uint8_t* data, size_t size)
{
    if (data == nullptr || size < kHeaderSize)
    {
        return std::nullopt;
    }

    uint32_t magic = ReadU32Le(data);
    if (magic != kSfoMagic)
    {
        return std::nullopt;
    }

    uint32_t key_table_offset = ReadU32Le(data + 8);
    uint32_t data_table_offset = ReadU32Le(data + 12);
    uint32_t num_entries = ReadU32Le(data + 16);

    if (key_table_offset < kHeaderSize ||
        key_table_offset > size ||
        data_table_offset < key_table_offset ||
        data_table_offset > size)
    {
        return std::nullopt;
    }

    if (kHeaderSize + num_entries * kEntrySize > key_table_offset)
    {
        return std::nullopt;
    }

    ParamSfo sfo;

    for (uint32_t i = 0; i < num_entries; ++i)
    {
        const uint8_t* entry_ptr = data + kHeaderSize + i * kEntrySize;
        uint16_t key_offset = ReadU16Le(entry_ptr);
        uint16_t data_format = ReadU16Le(entry_ptr + 2);
        uint32_t data_length = ReadU32Le(entry_ptr + 4);
        uint32_t max_data_length = ReadU32Le(entry_ptr + 8);
        uint32_t data_offset = ReadU32Le(entry_ptr + 12);

        (void)max_data_length;

        size_t abs_key_offset = key_table_offset + key_offset;
        if (abs_key_offset >= data_table_offset)
        {
            return std::nullopt;
        }

        // Find null terminator for key string
        const char* key_start = reinterpret_cast<const char*>(data + abs_key_offset);
        size_t max_key_len = data_table_offset - abs_key_offset;
        const void* null_pos = std::memchr(key_start, '\0', max_key_len);
        if (null_pos == nullptr)
        {
            return std::nullopt;
        }
        std::string key(key_start, static_cast<const char*>(null_pos));

        size_t abs_data_offset = data_table_offset + data_offset;
        if (abs_data_offset + data_length > size)
        {
            return std::nullopt;
        }

        const uint8_t* val_ptr = data + abs_data_offset;
        SfoEntry entry;

        if (data_format == kFormatUtf8String || data_format == kFormatUtf8Special)
        {
            entry.type = SfoValueType::kUtf8String;
            const char* str_start = reinterpret_cast<const char*>(val_ptr);
            const void* str_null = std::memchr(str_start, '\0', data_length);
            size_t str_len = (str_null != nullptr)
                ? static_cast<size_t>(static_cast<const char*>(str_null) - str_start)
                : data_length;
            entry.value = std::string(str_start, str_len);
        }
        else if (data_format == kFormatInteger)
        {
            if (data_length < 4)
            {
                return std::nullopt;
            }
            entry.type = SfoValueType::kInteger;
            entry.value = ReadU32Le(val_ptr);
        }
        else
        {
            entry.type = SfoValueType::kBinary;
            entry.value = std::vector<uint8_t>(val_ptr, val_ptr + data_length);
        }

        sfo.entries_[std::move(key)] = std::move(entry);
    }

    return sfo;
}

bool ParamSfo::HasKey(const std::string& key) const
{
    return entries_.find(key) != entries_.end();
}

std::optional<std::string> ParamSfo::GetString(const std::string& key) const
{
    auto it = entries_.find(key);
    if (it != entries_.end() && it->second.type == SfoValueType::kUtf8String)
    {
        return std::get<std::string>(it->second.value);
    }
    return std::nullopt;
}

std::optional<uint32_t> ParamSfo::GetInteger(const std::string& key) const
{
    auto it = entries_.find(key);
    if (it != entries_.end() && it->second.type == SfoValueType::kInteger)
    {
        return std::get<uint32_t>(it->second.value);
    }
    return std::nullopt;
}

} // namespace repsp::module
