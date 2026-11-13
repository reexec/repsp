#include <repsp/module/prx.h>
#include <repsp/runtime/guest_memory.h>
#include <repsp/runtime/prx_loader.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#define CHECK(cond)                                                                      \
    do                                                                                   \
    {                                                                                    \
        if (!(cond))                                                                     \
        {                                                                                \
            std::cerr << "CHECK failed: " #cond " at " << __LINE__ << std::endl;         \
            return 1;                                                                    \
        }                                                                                \
    } while (0)

namespace
{

void Write16(std::vector<uint8_t>& buf, size_t offset, uint16_t val)
{
    if (offset + 2 > buf.size())
    {
        buf.resize(offset + 2, 0);
    }
    buf[offset + 0] = static_cast<uint8_t>(val);
    buf[offset + 1] = static_cast<uint8_t>(val >> 8);
}

void Write32(std::vector<uint8_t>& buf, size_t offset, uint32_t val)
{
    if (offset + 4 > buf.size())
    {
        buf.resize(offset + 4, 0);
    }
    buf[offset + 0] = static_cast<uint8_t>(val);
    buf[offset + 1] = static_cast<uint8_t>(val >> 8);
    buf[offset + 2] = static_cast<uint8_t>(val >> 16);
    buf[offset + 3] = static_cast<uint8_t>(val >> 24);
}

uint32_t Read32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Build a valid synthetic PRX in memory
std::vector<uint8_t> BuildSyntheticPrx()
{
    std::vector<uint8_t> prx(0x500, 0);

    // ELF Header at 0x00
    prx[0] = 0x7F;
    prx[1] = 'E';
    prx[2] = 'L';
    prx[3] = 'F';
    prx[4] = 1; // ELFCLASS32
    prx[5] = 1; // ELFDATA2LSB
    prx[6] = 1; // EV_CURRENT

    Write16(prx, 16, 0xFFA0);       // e_type = ET_SCE_EXEC
    Write16(prx, 18, 8);            // e_machine = EM_MIPS
    Write32(prx, 20, 1);            // e_version = 1
    Write32(prx, 24, 0x00000000);   // e_entry = 0x00
    Write32(prx, 28, 52);           // e_phoff = 52
    Write32(prx, 32, 116);          // e_shoff = 116 (52 + 64)
    Write32(prx, 36, 0);            // e_flags = 0
    Write16(prx, 40, 52);           // e_ehsize = 52
    Write16(prx, 42, 32);           // e_phentsize = 32
    Write16(prx, 44, 2);            // e_phnum = 2
    Write16(prx, 46, 40);           // e_shentsize = 40
    Write16(prx, 48, 4);            // e_shnum = 4
    Write16(prx, 50, 1);            // e_shstrndx = 1

    // Program Header 0: Segment 0 (Code/Data)
    // Offset 52
    Write32(prx, 52 + 0, 1);            // p_type = PT_LOAD
    Write32(prx, 52 + 4, 0x200);        // p_offset = 0x200
    Write32(prx, 52 + 8, 0x00000000);   // p_vaddr = 0x0
    Write32(prx, 52 + 12, 0x00000040);  // p_paddr = 0x40 (sceModuleInfo)
    Write32(prx, 52 + 16, 0x200);       // p_filesz = 0x200 (512 bytes)
    Write32(prx, 52 + 20, 0x200);       // p_memsz = 0x200 (512 bytes)
    Write32(prx, 52 + 24, 7);           // p_flags = RWX
    Write32(prx, 52 + 28, 16);          // p_align = 16

    // Program Header 1: Segment 1 (Data/BSS)
    // Offset 52 + 32 = 84
    Write32(prx, 84 + 0, 1);            // p_type = PT_LOAD
    Write32(prx, 84 + 4, 0x400);        // p_offset = 0x400
    Write32(prx, 84 + 8, 0x00001000);   // p_vaddr = 0x1000
    Write32(prx, 84 + 12, 0);           // p_paddr = 0
    Write32(prx, 84 + 16, 0x100);       // p_filesz = 0x100 (256 bytes)
    Write32(prx, 84 + 20, 0x300);       // p_memsz = 0x300 (768 bytes, 512 bytes BSS)
    Write32(prx, 84 + 24, 6);           // p_flags = RW
    Write32(prx, 84 + 28, 16);          // p_align = 16

    // Section string table at 0x120
    const char strtab[] = "\0.shstrtab\0.reloc\0.rodata.sceModuleInfo\0";
    size_t strtab_sz = sizeof(strtab);
    std::memcpy(&prx[0x120], strtab, strtab_sz);

    // Section Header 0: NULL (Offset 116)
    // Section Header 1: .shstrtab (Offset 116 + 40 = 156)
    Write32(prx, 156 + 0, 1);           // sh_name (".shstrtab")
    Write32(prx, 156 + 4, 3);           // sh_type = SHT_STRTAB
    Write32(prx, 156 + 8, 0);           // sh_flags
    Write32(prx, 156 + 12, 0);          // sh_addr
    Write32(prx, 156 + 16, 0x120);      // sh_offset
    Write32(prx, 156 + 20, static_cast<uint32_t>(strtab_sz)); // sh_size
    Write32(prx, 156 + 24, 0);          // sh_link
    Write32(prx, 156 + 28, 0);          // sh_info
    Write32(prx, 156 + 32, 1);          // sh_addralign
    Write32(prx, 156 + 36, 0);          // sh_entsize

    // Section Header 2: .reloc (Offset 196)
    Write32(prx, 196 + 0, 11);          // sh_name (".reloc")
    Write32(prx, 196 + 4, 0x700000A0);  // sh_type = SHT_PRXRELOC
    Write32(prx, 196 + 8, 0);           // sh_flags
    Write32(prx, 196 + 12, 0);          // sh_addr
    Write32(prx, 196 + 16, 0x180);      // sh_offset
    Write32(prx, 196 + 20, 5 * 8);      // sh_size = 40 bytes (5 entries)
    Write32(prx, 196 + 24, 0);          // sh_link
    Write32(prx, 196 + 28, 0);          // sh_info
    Write32(prx, 196 + 32, 4);          // sh_addralign
    Write32(prx, 196 + 36, 8);          // sh_entsize

    // Section Header 3: .rodata.sceModuleInfo (Offset 236)
    Write32(prx, 236 + 0, 18);          // sh_name (".rodata.sceModuleInfo")
    Write32(prx, 236 + 4, 1);           // sh_type = SHT_PROGBITS
    Write32(prx, 236 + 8, 2);           // sh_flags = SHF_ALLOC
    Write32(prx, 236 + 12, 0x40);       // sh_addr = 0x40
    Write32(prx, 236 + 16, 0x240);      // sh_offset = 0x200 + 0x40
    Write32(prx, 236 + 20, 52);         // sh_size = 52
    Write32(prx, 236 + 24, 0);          // sh_link
    Write32(prx, 236 + 28, 0);          // sh_info
    Write32(prx, 236 + 32, 4);          // sh_addralign
    Write32(prx, 236 + 36, 0);          // sh_entsize

    // Relocations at 0x180 (5 entries):
    // 0: R_MIPS_32 at 0x10, addrbase=1, ofsbase=0
    Write32(prx, 0x180 + 0, 0x10);
    Write32(prx, 0x180 + 4, (1 << 16) | (0 << 8) | 2);

    // 1: R_MIPS_26 at 0x14, addrbase=0, ofsbase=0
    Write32(prx, 0x180 + 8, 0x14);
    Write32(prx, 0x180 + 12, (0 << 16) | (0 << 8) | 4);

    // 2: R_MIPS_HI16 at 0x18, addrbase=1, ofsbase=0
    Write32(prx, 0x180 + 16, 0x18);
    Write32(prx, 0x180 + 20, (1 << 16) | (0 << 8) | 5);

    // 3: R_MIPS_LO16 at 0x1C (paired with HI16), addrbase=1, ofsbase=0
    Write32(prx, 0x180 + 24, 0x1C);
    Write32(prx, 0x180 + 28, (1 << 16) | (0 << 8) | 6);

    // 4: R_MIPS_LO16 at 0x20 (lone LO16), addrbase=1, ofsbase=0
    Write32(prx, 0x180 + 32, 0x20);
    Write32(prx, 0x180 + 36, (1 << 16) | (0 << 8) | 6);

    // Segment 0 data at 0x200
    // 0x10: word = 0x00000020
    Write32(prx, 0x200 + 0x10, 0x00000020);
    // 0x14: jal 0x10 -> (0x0C000000 | (0x10 >> 2)) = 0x0C000004
    Write32(prx, 0x200 + 0x14, 0x0C000004);
    // 0x18: lui $a0, 0x0001
    Write32(prx, 0x200 + 0x18, 0x3C040001);
    // 0x1C: addiu $a0, $a0, -0x7000 (0x9000 unsigned)
    Write32(prx, 0x200 + 0x1C, 0x24849000);
    // 0x20: lw $a0, 0x0010($a0)
    Write32(prx, 0x200 + 0x20, 0x8C840010);

    // Segment 0 sceModuleInfo at 0x200 + 0x40 (0x240):
    Write16(prx, 0x240 + 0x00, 0);      // attribute = 0
    prx[0x240 + 0x02] = 1;              // version[0] = 1
    prx[0x240 + 0x03] = 0;              // version[1] = 0
    std::memcpy(&prx[0x240 + 0x04], "test_mod", 9);
    Write32(prx, 0x240 + 0x20, 0x00001050); // gp = 0x1050 (in segment 1)
    Write32(prx, 0x240 + 0x24, 0);          // ent_top = 0
    Write32(prx, 0x240 + 0x28, 0);          // ent_end = 0
    Write32(prx, 0x240 + 0x2C, 0x80);       // stub_top = 0x80
    Write32(prx, 0x240 + 0x30, 0x94);       // stub_end = 0x94 (20 bytes)

    // Segment 0 stub table at 0x200 + 0x80 (0x280):
    Write32(prx, 0x280 + 0x00, 0xA0);       // name_ptr = 0xA0
    Write16(prx, 0x280 + 0x04, 0);          // version = 0
    Write16(prx, 0x280 + 0x06, 0);          // attribute = 0
    prx[0x280 + 0x08] = 5;                  // ent_len = 5 (20 bytes)
    prx[0x280 + 0x09] = 0;                  // var_cnt = 0
    Write16(prx, 0x280 + 0x0A, 1);          // func_cnt = 1
    Write32(prx, 0x280 + 0x0C, 0xB0);       // nid_tbl = 0xB0
    Write32(prx, 0x280 + 0x10, 0xC0);       // stub_tbl = 0xC0

    // Segment 0 library name string at 0x200 + 0xA0 (0x2A0):
    std::memcpy(&prx[0x2A0], "test_lib", 9);

    // Segment 0 NID table at 0x200 + 0xB0 (0x2B0):
    Write32(prx, 0x2B0, 0x12345678);

    // Segment 0 stub thunk at 0x200 + 0xC0 (0x2C0):
    Write32(prx, 0x2C0 + 0, 0x03E00008); // jr $ra
    Write32(prx, 0x2C0 + 4, 0x00000000); // nop

    return prx;
}

} // namespace

int main()
{
    std::cout << "[prx_probe] Starting PRX loader probe verification..." << std::endl;

    const uint32_t kBase = 0x08900000;
    auto prx = BuildSyntheticPrx();

    // 1. Verify ParsePrx
    std::string err;
    auto parsed = repsp::module::ParsePrx(prx, kBase, &err);
    if (!parsed)
    {
        std::cerr << "FAILED: ParsePrx returned nullopt: " << err << std::endl;
        return 1;
    }

    CHECK(parsed->base == kBase);
    CHECK(parsed->entry == kBase + 0x00);
    CHECK(parsed->segments.size() == 2);
    CHECK(parsed->segments[0].vaddr == 0x00000000);
    CHECK(parsed->segments[0].file_size == 0x200);
    CHECK(parsed->segments[1].vaddr == 0x00001000);
    CHECK(parsed->segments[1].mem_size == 0x300);
    CHECK(parsed->image_end == kBase + 0x1300);
    CHECK(parsed->relocations_applied == 5);
    CHECK(parsed->info.name == "test_mod");
    CHECK(parsed->imports.size() == 1);
    CHECK(parsed->imports[0].library == "test_lib");
    CHECK(parsed->imports[0].nid == 0x12345678);
    CHECK(parsed->imports[0].stub_address == kBase + 0xC0);

    std::cout << "[prx_probe] ParsePrx succeeded. Relocations applied: "
              << parsed->relocations_applied << std::endl;

    // 2. Verify RelocateToBuffer
    auto buf = repsp::module::RelocateToBuffer(prx, kBase, &err);
    if (!buf)
    {
        std::cerr << "FAILED: RelocateToBuffer returned nullopt: " << err << std::endl;
        return 1;
    }
    CHECK(buf->size() == 0x1300);

    // S0 = 0x08900000, S1 = 0x08901000
    // Case 0: R_MIPS_32 at 0x10 with addrbase 1 -> word was 0x20 -> 0x08901020
    uint32_t w0 = Read32(buf->data() + 0x10);
    CHECK(w0 == 0x08901020);

    // Case 1: R_MIPS_26 at 0x14 with addrbase 0 -> word was 0x0C000004 -> target 0x08900010 -> 0x0E240004
    uint32_t w1 = Read32(buf->data() + 0x14);
    CHECK(w1 == 0x0E240004);

    // Case 2 & 3: HI16 (0x18) / LO16 (0x1C) with addrbase 1 and carry:
    // hi was 1, lo was 0x9000 (-0x7000). addend = 0x9000. value = 0x9000 + 0x08901000 = 0x0890A000.
    // new_hi = (0x0890A000 + 0x8000) >> 16 = 0x0891.
    // new_lo = (0x9000 + 0x08901000) & 0xFFFF = 0xA000.
    uint32_t w2 = Read32(buf->data() + 0x18);
    uint32_t w3 = Read32(buf->data() + 0x1C);
    CHECK((w2 & 0xFFFF) == 0x0891);
    CHECK((w3 & 0xFFFF) == 0xA000);

    // Case 4: lone LO16 at 0x20 with addrbase 1:
    // lo was 0x0010. S1 = 0x08901000. new_lo = (0x0010 + 0x1000) & 0xFFFF = 0x1010.
    uint32_t w4 = Read32(buf->data() + 0x20);
    CHECK((w4 & 0xFFFF) == 0x1010);

    // Verify BSS is zeroed in buffer
    for (size_t b = 0x1100; b < 0x1300; ++b)
    {
        CHECK((*buf)[b] == 0);
    }

    std::cout << "[prx_probe] RelocateToBuffer relocation and BSS checks passed." << std::endl;

    // 3. Verify LoadPrx into GuestMemory
    repsp::runtime::GuestMemory memory;
    if (!memory.Initialize())
    {
        std::cerr << "FAILED: GuestMemory initialization failed" << std::endl;
        return 1;
    }

    auto loaded = repsp::runtime::LoadPrx(prx, kBase, memory, &err);
    if (!loaded)
    {
        std::cerr << "FAILED: LoadPrx returned nullopt: " << err << std::endl;
        return 1;
    }

    CHECK(memory.ReadU32(kBase + 0x10) == 0x08901020);
    CHECK(memory.ReadU32(kBase + 0x14) == 0x0E240004);
    CHECK((memory.ReadU32(kBase + 0x18) & 0xFFFF) == 0x0891);
    CHECK((memory.ReadU32(kBase + 0x1C) & 0xFFFF) == 0xA000);
    CHECK((memory.ReadU32(kBase + 0x20) & 0xFFFF) == 0x1010);
    std::cout << "[prx_probe] LoadPrx guest memory verification passed." << std::endl;

    // 4. Bad input tests
    // 4.1 Short file
    std::vector<uint8_t> short_file(20, 0);
    CHECK(!repsp::module::ParsePrx(short_file, kBase, &err));

    // 4.2 Bad magic
    auto bad_magic = prx;
    bad_magic[0] = 0x00;
    CHECK(!repsp::module::ParsePrx(bad_magic, kBase, &err));

    // 4.3 Unknown relocation type
    auto bad_reloc_type = prx;
    bad_reloc_type[0x180 + 4] = 99; // type = 99
    CHECK(!repsp::module::ParsePrx(bad_reloc_type, kBase, &err));

    // 4.4 Compressed PRX relocations (0x700000A1)
    auto compressed_reloc = prx;
    Write32(compressed_reloc, 196 + 4, 0x700000A1);
    CHECK(!repsp::module::ParsePrx(compressed_reloc, kBase, &err));

    // 4.5 Relocation r_offset out of range
    auto bad_offset = prx;
    Write32(bad_offset, 0x180 + 0, 0x10000); // 64 KiB > 512 bytes mem_size
    CHECK(!repsp::module::ParsePrx(bad_offset, kBase, &err));

    // 4.6 LoadPrx outside valid guest memory
    CHECK(!repsp::runtime::LoadPrx(prx, 0x30000000, memory, &err));

    std::cout << "[prx_probe] All bad input rejection tests passed." << std::endl;
    std::cout << "[prx_probe] ALL CHECKS PASSED!" << std::endl;

    return 0;
}
