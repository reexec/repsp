#include "repsp/module/param_sfo.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define TEST_ASSERT(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::exit(1); \
        } \
    } while (0)

namespace
{

void WriteU16Le(std::vector<uint8_t>& buf, size_t offset, uint16_t val)
{
    buf[offset + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

void WriteU32Le(std::vector<uint8_t>& buf, size_t offset, uint32_t val)
{
    buf[offset + 0] = static_cast<uint8_t>(val & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

std::vector<uint8_t> CreateSyntheticSfo()
{
    // Layout:
    // 0..19: Header (20 bytes)
    // 20..51: 2 Entries (32 bytes)
    // 52..69: Key table: "TITLE\0" (6 bytes), "DISC_VERSION\0" (13 bytes) -> 19 bytes
    // 72: Data table (aligned to 4):
    //   data 0: "Synthetic Game\0" (15 bytes)
    //   data 1: uint32 0x00010005 (4 bytes)

    const char key0[] = "TITLE";
    const char key1[] = "DISC_VERSION";
    const char val0[] = "Synthetic Game";
    const uint32_t val1 = 0x00010005;

    size_t header_size = 20;
    size_t entries_size = 2 * 16;
    size_t key_table_offset = header_size + entries_size; // 52

    size_t key0_offset = 0;
    size_t key1_offset = sizeof(key0); // 6
    size_t key_table_size = sizeof(key0) + sizeof(key1); // 19

    size_t data_table_offset = key_table_offset + key_table_size;
    while ((data_table_offset % 4) != 0)
    {
        ++data_table_offset;
    }

    size_t data0_offset = 0;
    size_t data0_len = sizeof(val0);
    size_t data1_offset = data0_offset + data0_len;
    while ((data1_offset % 4) != 0)
    {
        ++data1_offset;
    }
    size_t data1_len = 4;
    size_t total_size = data_table_offset + data1_offset + data1_len;

    std::vector<uint8_t> buf(total_size, 0);

    // Header
    buf[0] = 0x00;
    buf[1] = 'P';
    buf[2] = 'S';
    buf[3] = 'F';
    WriteU32Le(buf, 4, 0x00000101); // Version 1.1
    WriteU32Le(buf, 8, static_cast<uint32_t>(key_table_offset));
    WriteU32Le(buf, 12, static_cast<uint32_t>(data_table_offset));
    WriteU32Le(buf, 16, 2); // 2 entries

    // Entry 0: TITLE (string, 0x0204)
    size_t e0 = header_size;
    WriteU16Le(buf, e0 + 0, static_cast<uint16_t>(key0_offset));
    WriteU16Le(buf, e0 + 2, 0x0204);
    WriteU32Le(buf, e0 + 4, static_cast<uint32_t>(data0_len));
    WriteU32Le(buf, e0 + 8, static_cast<uint32_t>(data0_len));
    WriteU32Le(buf, e0 + 12, static_cast<uint32_t>(data0_offset));

    // Entry 1: DISC_VERSION (integer, 0x0404)
    size_t e1 = header_size + 16;
    WriteU16Le(buf, e1 + 0, static_cast<uint16_t>(key1_offset));
    WriteU16Le(buf, e1 + 2, 0x0404);
    WriteU32Le(buf, e1 + 4, static_cast<uint32_t>(data1_len));
    WriteU32Le(buf, e1 + 8, static_cast<uint32_t>(data1_len));
    WriteU32Le(buf, e1 + 12, static_cast<uint32_t>(data1_offset));

    // Keys
    std::memcpy(buf.data() + key_table_offset + key0_offset, key0, sizeof(key0));
    std::memcpy(buf.data() + key_table_offset + key1_offset, key1, sizeof(key1));

    // Data
    std::memcpy(buf.data() + data_table_offset + data0_offset, val0, sizeof(val0));
    WriteU32Le(buf, data_table_offset + data1_offset, val1);

    return buf;
}

} // namespace

int main()
{
    using namespace repsp::module;

    // 1. Parse synthetic valid SFO
    std::vector<uint8_t> valid_sfo = CreateSyntheticSfo();
    auto sfo_opt = ParamSfo::Parse(valid_sfo.data(), valid_sfo.size());
    TEST_ASSERT(sfo_opt.has_value());

    const auto& sfo = *sfo_opt;
    TEST_ASSERT(sfo.HasKey("TITLE"));
    TEST_ASSERT(sfo.HasKey("DISC_VERSION"));
    TEST_ASSERT(!sfo.HasKey("NON_EXISTENT_KEY"));

    auto title_opt = sfo.GetString("TITLE");
    TEST_ASSERT(title_opt.has_value());
    TEST_ASSERT(*title_opt == "Synthetic Game");

    auto ver_opt = sfo.GetInteger("DISC_VERSION");
    TEST_ASSERT(ver_opt.has_value());
    TEST_ASSERT(*ver_opt == 0x00010005);

    // 2. Reject invalid inputs
    TEST_ASSERT(!ParamSfo::Parse(nullptr, 0).has_value());
    TEST_ASSERT(!ParamSfo::Parse(valid_sfo.data(), 10).has_value()); // Truncated header

    std::vector<uint8_t> bad_magic = valid_sfo;
    bad_magic[0] = 'X';
    TEST_ASSERT(!ParamSfo::Parse(bad_magic.data(), bad_magic.size()).has_value());

    std::vector<uint8_t> bad_offset = valid_sfo;
    WriteU32Le(bad_offset, 12, static_cast<uint32_t>(valid_sfo.size() + 100)); // Out-of-bounds data offset
    TEST_ASSERT(!ParamSfo::Parse(bad_offset.data(), bad_offset.size()).has_value());

    std::printf("module_probe passed.\n");
    return 0;
}
