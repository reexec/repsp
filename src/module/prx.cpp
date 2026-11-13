#include <repsp/module/prx.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace repsp::module
{

namespace
{

constexpr uint32_t kElfMagic = 0x464C457F; // 0x7F 'E' 'L' 'F'
constexpr uint16_t kTypePrx = 0xFFA0;
constexpr uint16_t kMachineMips = 8;
constexpr uint32_t kPtLoad = 1;
constexpr uint32_t kShtPrxReloc = 0x700000A0;
constexpr uint32_t kShtPrxRelocCompressed = 0x700000A1;

constexpr uint8_t kR_MIPS_32 = 2;
constexpr uint8_t kR_MIPS_26 = 4;
constexpr uint8_t kR_MIPS_HI16 = 5;
constexpr uint8_t kR_MIPS_LO16 = 6;

inline uint16_t Read16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t Read32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline void Write32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

void SetError(std::string* error, const std::string& msg)
{
    if (error != nullptr)
    {
        *error = msg;
    }
}

struct RelocateResult
{
    std::vector<uint8_t> buffer;
    std::vector<PrxSegment> segments;
    uint32_t entry = 0;
    size_t relocations_applied = 0;
    uint32_t modinfo_offset = 0;
    bool has_modinfo = false;
};

std::optional<RelocateResult> ProcessPrx(std::span<const uint8_t> file, uint32_t base,
                                        std::string* error)
{
    if (file.size() < 52)
    {
        SetError(error, "File is too short to contain an ELF header");
        return std::nullopt;
    }

    const uint8_t* data = file.data();
    if (Read32(data) != kElfMagic)
    {
        SetError(error, "Invalid ELF magic");
        return std::nullopt;
    }

    if (data[4] != 1) // ELFCLASS32
    {
        SetError(error, "Unsupported ELF class (must be 32-bit)");
        return std::nullopt;
    }

    if (data[5] != 1) // ELFDATA2LSB
    {
        SetError(error, "Unsupported ELF endianness (must be little-endian)");
        return std::nullopt;
    }

    uint16_t e_type = Read16(data + 16);
    if (e_type != kTypePrx)
    {
        SetError(error, "Unsupported ELF type (expected PRX e_type 0xFFA0)");
        return std::nullopt;
    }

    uint16_t e_machine = Read16(data + 18);
    if (e_machine != kMachineMips)
    {
        SetError(error, "Unsupported machine architecture (expected MIPS)");
        return std::nullopt;
    }

    uint32_t e_entry = Read32(data + 24);
    uint32_t e_phoff = Read32(data + 28);
    uint32_t e_shoff = Read32(data + 32);
    uint16_t e_phentsize = Read16(data + 42);
    uint16_t e_phnum = Read16(data + 44);
    uint16_t e_shentsize = Read16(data + 46);
    uint16_t e_shnum = Read16(data + 48);
    uint16_t e_shstrndx = Read16(data + 50);

    if (e_phnum == 0 || e_phentsize < 32)
    {
        SetError(error, "Invalid program header parameters");
        return std::nullopt;
    }

    if (static_cast<size_t>(e_phoff) + static_cast<size_t>(e_phnum) * e_phentsize > file.size())
    {
        SetError(error, "Program header table exceeds file size");
        return std::nullopt;
    }

    std::vector<PrxSegment> segments;
    uint32_t image_size = 0;
    uint32_t first_paddr = 0;

    for (uint16_t i = 0; i < e_phnum; ++i)
    {
        const uint8_t* ph = data + e_phoff + static_cast<size_t>(i) * e_phentsize;
        uint32_t p_type = Read32(ph + 0);
        uint32_t p_offset = Read32(ph + 4);
        uint32_t p_vaddr = Read32(ph + 8);
        uint32_t p_paddr = Read32(ph + 12);
        uint32_t p_filesz = Read32(ph + 16);
        uint32_t p_memsz = Read32(ph + 20);

        if (i == 0)
        {
            first_paddr = p_paddr;
        }

        if (p_type == kPtLoad)
        {
            if (static_cast<size_t>(p_offset) + p_filesz > file.size())
            {
                SetError(error, "Segment file data exceeds file size");
                return std::nullopt;
            }
            if (p_filesz > p_memsz)
            {
                SetError(error, "Segment file size exceeds memory size");
                return std::nullopt;
            }

            segments.push_back(PrxSegment{p_vaddr, p_offset, p_filesz, p_memsz});
            uint32_t seg_end = p_vaddr + p_memsz;
            if (seg_end > image_size)
            {
                image_size = seg_end;
            }
        }
    }

    if (segments.empty())
    {
        SetError(error, "No PT_LOAD segments found in PRX");
        return std::nullopt;
    }

    // Check section headers
    if (e_shnum > 0)
    {
        if (e_shentsize < 40 ||
            static_cast<size_t>(e_shoff) + static_cast<size_t>(e_shnum) * e_shentsize > file.size())
        {
            SetError(error, "Section header table exceeds file size");
            return std::nullopt;
        }

        for (uint16_t i = 0; i < e_shnum; ++i)
        {
            const uint8_t* sh = data + e_shoff + static_cast<size_t>(i) * e_shentsize;
            uint32_t sh_type = Read32(sh + 4);
            if (sh_type == kShtPrxRelocCompressed)
            {
                SetError(error, "Compressed PRX relocations (0x700000A1) are not supported");
                return std::nullopt;
            }
            if (sh_type == 8 /* SHT_NOBITS */)
            {
                continue;
            }
            uint32_t sh_offset = Read32(sh + 16);
            uint32_t sh_size = Read32(sh + 20);
            if (static_cast<size_t>(sh_offset) + sh_size > file.size())
            {
                SetError(error, "Section contents exceed file size");
                return std::nullopt;
            }
        }
    }

    // Allocate image buffer and copy segment contents
    std::vector<uint8_t> buffer(image_size, 0);
    for (const auto& seg : segments)
    {
        if (seg.file_size > 0)
        {
            std::memcpy(buffer.data() + seg.vaddr, data + seg.file_offset, seg.file_size);
        }
    }

    // Apply relocations from SHT_PRXRELOC sections
    size_t relocations_applied = 0;
    if (e_shnum > 0)
    {
        for (uint16_t s = 0; s < e_shnum; ++s)
        {
            const uint8_t* sh = data + e_shoff + static_cast<size_t>(s) * e_shentsize;
            uint32_t sh_type = Read32(sh + 4);
            if (sh_type != kShtPrxReloc)
            {
                continue;
            }

            uint32_t sh_offset = Read32(sh + 16);
            uint32_t sh_size = Read32(sh + 20);
            if (sh_size % 8 != 0)
            {
                SetError(error, "Malformed PRX relocation section size");
                return std::nullopt;
            }

            size_t num_relocs = sh_size / 8;
            const uint8_t* rel_entries = data + sh_offset;

            for (size_t r = 0; r < num_relocs; ++r)
            {
                uint32_t r_offset = Read32(rel_entries + r * 8);
                uint32_t r_info = Read32(rel_entries + r * 8 + 4);

                uint8_t type = static_cast<uint8_t>(r_info & 0xFF);
                uint8_t ofsbase = static_cast<uint8_t>((r_info >> 8) & 0xFF);
                uint8_t addrbase = static_cast<uint8_t>((r_info >> 16) & 0xFF);

                if (ofsbase >= segments.size())
                {
                    SetError(error, "Relocation ofsbase segment index out of range");
                    return std::nullopt;
                }
                if (addrbase >= segments.size())
                {
                    SetError(error, "Relocation addrbase segment index out of range");
                    return std::nullopt;
                }

                if (r_offset + 4 > segments[ofsbase].mem_size)
                {
                    SetError(error, "Relocation r_offset exceeds segment memory size");
                    return std::nullopt;
                }

                uint32_t patch_offset = segments[ofsbase].vaddr + r_offset;
                if (patch_offset + 4 > buffer.size())
                {
                    SetError(error, "Relocation patch address exceeds image size");
                    return std::nullopt;
                }

                uint32_t S = base + segments[addrbase].vaddr;

                switch (type)
                {
                case kR_MIPS_32:
                {
                    uint32_t word = Read32(buffer.data() + patch_offset);
                    word += S;
                    Write32(buffer.data() + patch_offset, word);
                    relocations_applied++;
                    break;
                }
                case kR_MIPS_26:
                {
                    uint32_t word = Read32(buffer.data() + patch_offset);
                    uint32_t target = ((word & 0x03FFFFFF) << 2) + S;
                    word = (word & 0xFC000000) | ((target >> 2) & 0x03FFFFFF);
                    Write32(buffer.data() + patch_offset, word);
                    relocations_applied++;
                    break;
                }
                case kR_MIPS_HI16:
                {
                    // Find matching LO16 in the subsequent relocation entries
                    bool found_lo = false;
                    uint32_t lo_patch_offset = 0;

                    for (size_t k = r + 1; k < num_relocs; ++k)
                    {
                        uint32_t next_info = Read32(rel_entries + k * 8 + 4);
                        if ((next_info & 0xFF) == kR_MIPS_LO16)
                        {
                            uint32_t lo_offset = Read32(rel_entries + k * 8);
                            uint8_t lo_ofsbase = static_cast<uint8_t>((next_info >> 8) & 0xFF);
                            if (lo_ofsbase >= segments.size())
                            {
                                SetError(error, "Paired LO16 ofsbase out of range");
                                return std::nullopt;
                            }
                            if (lo_offset + 4 > segments[lo_ofsbase].mem_size)
                            {
                                SetError(error, "Paired LO16 offset exceeds segment memory size");
                                return std::nullopt;
                            }
                            lo_patch_offset = segments[lo_ofsbase].vaddr + lo_offset;
                            if (lo_patch_offset + 4 > buffer.size())
                            {
                                SetError(error, "Paired LO16 patch offset exceeds image size");
                                return std::nullopt;
                            }
                            found_lo = true;
                            break;
                        }
                    }

                    if (!found_lo)
                    {
                        SetError(error, "R_MIPS_HI16 without matching LO16 relocation");
                        return std::nullopt;
                    }

                    uint32_t hi_word = Read32(buffer.data() + patch_offset);
                    uint32_t lo_word = Read32(buffer.data() + lo_patch_offset);
                    uint32_t hi = hi_word & 0xFFFF;
                    int16_t lo = static_cast<int16_t>(lo_word & 0xFFFF);
                    int32_t addend = static_cast<int32_t>((hi << 16) + lo);
                    uint32_t value = static_cast<uint32_t>(addend) + S;
                    uint32_t new_hi = ((value + 0x8000) >> 16) & 0xFFFF;
                    hi_word = (hi_word & 0xFFFF0000) | new_hi;
                    Write32(buffer.data() + patch_offset, hi_word);
                    relocations_applied++;
                    break;
                }
                case kR_MIPS_LO16:
                {
                    uint32_t lo_word = Read32(buffer.data() + patch_offset);
                    uint32_t lo = lo_word & 0xFFFF;
                    uint32_t new_lo = (lo + S) & 0xFFFF;
                    lo_word = (lo_word & 0xFFFF0000) | new_lo;
                    Write32(buffer.data() + patch_offset, lo_word);
                    relocations_applied++;
                    break;
                }
                default:
                    SetError(error, "Unsupported relocation type: " + std::to_string(type));
                    return std::nullopt;
                }
            }
        }
    }

    // Locate .rodata.sceModuleInfo
    uint32_t modinfo_offset = 0;
    bool has_modinfo = false;

    if (e_shnum > 0 && e_shstrndx < e_shnum)
    {
        const uint8_t* strtab_sh = data + e_shoff + static_cast<size_t>(e_shstrndx) * e_shentsize;
        uint32_t strtab_off = Read32(strtab_sh + 16);
        uint32_t strtab_sz = Read32(strtab_sh + 20);

        if (static_cast<size_t>(strtab_off) + strtab_sz <= file.size())
        {
            const char* strtab = reinterpret_cast<const char*>(data + strtab_off);
            for (uint16_t i = 0; i < e_shnum; ++i)
            {
                const uint8_t* sh = data + e_shoff + static_cast<size_t>(i) * e_shentsize;
                uint32_t name_idx = Read32(sh + 0);
                if (name_idx < strtab_sz)
                {
                    const char* sname = strtab + name_idx;
                    if (std::strncmp(sname, ".rodata.sceModuleInfo", 22) == 0)
                    {
                        modinfo_offset = Read32(sh + 12); // sh_addr
                        has_modinfo = true;
                        break;
                    }
                }
            }
        }
    }

    if (!has_modinfo && first_paddr != 0)
    {
        modinfo_offset = first_paddr;
        has_modinfo = true;
    }

    RelocateResult result;
    result.buffer = std::move(buffer);
    result.segments = std::move(segments);
    result.entry = base + result.segments[0].vaddr + e_entry;
    result.relocations_applied = relocations_applied;
    result.modinfo_offset = modinfo_offset;
    result.has_modinfo = has_modinfo;

    return result;
}

} // namespace

std::optional<std::vector<uint8_t>> RelocateToBuffer(std::span<const uint8_t> file, uint32_t base,
                                                     std::string* error)
{
    auto res = ProcessPrx(file, base, error);
    if (!res)
    {
        return std::nullopt;
    }
    return std::move(res->buffer);
}

std::optional<LoadedPrx> ParsePrx(std::span<const uint8_t> file, uint32_t base,
                                 std::string* error)
{
    auto res = ProcessPrx(file, base, error);
    if (!res)
    {
        return std::nullopt;
    }

    const auto& buffer = res->buffer;
    LoadedPrx loaded;
    loaded.base = base;
    loaded.entry = res->entry;
    loaded.gp = 0;
    loaded.image_end = base + static_cast<uint32_t>(buffer.size());
    loaded.segments = std::move(res->segments);
    loaded.relocations_applied = res->relocations_applied;

    if (res->has_modinfo && res->modinfo_offset + 52 <= buffer.size())
    {
        const uint8_t* mi = buffer.data() + res->modinfo_offset;
        loaded.info.attribute = Read16(mi + 0x00);
        loaded.info.version[0] = mi[0x02];
        loaded.info.version[1] = mi[0x03];

        char name[29] = {0};
        std::memcpy(name, mi + 0x04, 28);
        loaded.info.name = name;

        loaded.info.gp = Read32(mi + 0x20);
        loaded.gp = loaded.info.gp;

        uint32_t ent_top = Read32(mi + 0x24);
        uint32_t ent_end = Read32(mi + 0x28);
        uint32_t stub_top = Read32(mi + 0x2C);
        uint32_t stub_end = Read32(mi + 0x30);

        uint32_t stub_top_off = (stub_top >= base) ? (stub_top - base) : stub_top;
        uint32_t stub_end_off = (stub_end >= base) ? (stub_end - base) : stub_end;
        uint32_t ent_top_off = (ent_top >= base) ? (ent_top - base) : ent_top;
        uint32_t ent_end_off = (ent_end >= base) ? (ent_end - base) : ent_end;

        // Parse imports
        if (stub_top_off < stub_end_off && stub_end_off <= buffer.size())
        {
            uint32_t curr = stub_top_off;
            while (curr + 20 <= stub_end_off)
            {
                uint32_t name_ptr = Read32(buffer.data() + curr + 0x00);
                uint8_t ent_len = buffer[curr + 0x08];
                uint16_t func_cnt = Read16(buffer.data() + curr + 0x0A);
                uint32_t nid_tbl = Read32(buffer.data() + curr + 0x0C);
                uint32_t stub_tbl = Read32(buffer.data() + curr + 0x10);

                uint32_t stride = ent_len ? (static_cast<uint32_t>(ent_len) * 4) : 20;
                if (stride < 20)
                {
                    break;
                }

                std::string lib_name;
                uint32_t name_off = (name_ptr >= base) ? (name_ptr - base) : name_ptr;
                if (name_off < buffer.size())
                {
                    const char* str = reinterpret_cast<const char*>(buffer.data() + name_off);
                    size_t max_len = buffer.size() - name_off;
                    size_t len = 0;
                    while (len < max_len && str[len] != '\0')
                    {
                        len++;
                    }
                    lib_name.assign(str, len);
                }

                uint32_t nid_off = (nid_tbl >= base) ? (nid_tbl - base) : nid_tbl;
                uint32_t stub_base_addr = (stub_tbl >= base) ? stub_tbl : (base + stub_tbl);

                for (uint16_t f = 0; f < func_cnt; ++f)
                {
                    if (nid_off + static_cast<size_t>(f) * 4 + 4 <= buffer.size())
                    {
                        uint32_t nid = Read32(buffer.data() + nid_off + f * 4);
                        uint32_t stub_addr = stub_base_addr + static_cast<uint32_t>(f) * 8;
                        loaded.imports.push_back(PrxImport{lib_name, nid, stub_addr});
                    }
                }

                curr += stride;
            }
        }

        // Parse exports
        if (ent_top_off < ent_end_off && ent_end_off <= buffer.size())
        {
            uint32_t curr = ent_top_off;
            while (curr + 16 <= ent_end_off)
            {
                uint32_t name_ptr = Read32(buffer.data() + curr + 0x00);
                uint8_t ent_len = buffer[curr + 0x08];
                uint8_t var_cnt = buffer[curr + 0x09];
                uint16_t func_cnt = Read16(buffer.data() + curr + 0x0A);
                uint32_t tbl = Read32(buffer.data() + curr + 0x0C);

                uint32_t stride = ent_len ? (static_cast<uint32_t>(ent_len) * 4) : 16;
                if (stride < 16)
                {
                    break;
                }

                std::string lib_name;
                if (name_ptr != 0)
                {
                    uint32_t name_off = (name_ptr >= base) ? (name_ptr - base) : name_ptr;
                    if (name_off < buffer.size())
                    {
                        const char* str = reinterpret_cast<const char*>(buffer.data() + name_off);
                        size_t max_len = buffer.size() - name_off;
                        size_t len = 0;
                        while (len < max_len && str[len] != '\0')
                        {
                            len++;
                        }
                        lib_name.assign(str, len);
                    }
                }

                uint32_t total = static_cast<uint32_t>(func_cnt) + var_cnt;
                uint32_t tbl_off = (tbl >= base) ? (tbl - base) : tbl;

                for (uint16_t f = 0; f < func_cnt; ++f)
                {
                    if (tbl_off + (static_cast<size_t>(total) + f + 1) * 4 <= buffer.size())
                    {
                        uint32_t nid = Read32(buffer.data() + tbl_off + f * 4);
                        uint32_t addr = Read32(buffer.data() + tbl_off + (total + f) * 4);
                        loaded.exports.push_back(PrxExport{lib_name, nid, addr});
                    }
                }

                curr += stride;
            }
        }
    }

    return loaded;
}

} // namespace repsp::module
