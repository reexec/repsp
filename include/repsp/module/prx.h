#ifndef REPSP_MODULE_PRX_H
#define REPSP_MODULE_PRX_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace repsp::module
{

struct PrxSegment
{
    uint32_t vaddr;
    uint32_t file_offset;
    uint32_t file_size;
    uint32_t mem_size;
};

struct PrxImport
{
    std::string library;
    uint32_t nid;
    uint32_t stub_address; // Stub address after relocation
};

struct PrxExport
{
    std::string library;
    uint32_t nid;
    uint32_t address;
};

struct PrxModuleInfo
{
    std::string name;
    uint16_t attribute;
    uint8_t version[2];
    uint32_t gp;
};

struct LoadedPrx
{
    uint32_t base;                      // Base address where segment 0 was placed
    uint32_t entry;                     // Relocated entry point (e_entry)
    uint32_t gp;                        // Relocated $gp value
    uint32_t image_end;                 // Base + highest segment end (including BSS)
    PrxModuleInfo info;
    std::vector<PrxSegment> segments;   // Link-time values
    std::vector<PrxImport> imports;
    std::vector<PrxExport> exports;
    size_t relocations_applied;
};

// Validates the ELF and reports what loading would produce, without modifying memory.
std::optional<LoadedPrx> ParsePrx(std::span<const uint8_t> file, uint32_t base,
                                 std::string* error = nullptr);

// Relocates the PRX into a byte buffer with segments laid out relative to base.
std::optional<std::vector<uint8_t>> RelocateToBuffer(std::span<const uint8_t> file, uint32_t base,
                                                     std::string* error = nullptr);

} // namespace repsp::module

#endif // REPSP_MODULE_PRX_H
