#include "repsp/runtime/guest_memory.h"
#include "repsp/platform/guarded_run.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define TEST_ASSERT(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::exit(1); \
        } \
    } while (0)

int main()
{
    using namespace repsp::runtime;
    GuestMemory mem;
    TEST_ASSERT(mem.Initialize());

    // 1. Translate in each valid physical region
    uint8_t* p_scratch = mem.Translate(GuestMemory::kScratchpadAddress);
    uint8_t* p_vram = mem.Translate(GuestMemory::kVramAddress);
    uint8_t* p_kram = mem.Translate(GuestMemory::kKernelRamAddress);
    uint8_t* p_uram = mem.Translate(GuestMemory::kUserRamAddress);

    TEST_ASSERT(p_scratch != nullptr);
    TEST_ASSERT(p_vram != nullptr);
    TEST_ASSERT(p_kram != nullptr);
    TEST_ASSERT(p_uram != nullptr);

    // 2. Mirror folding: 0x08800000, 0x48800000, 0x88800000 reach the same physical byte
    uint32_t addr_user = 0x08800000;
    uint32_t addr_uncached = 0x48800000;
    uint32_t addr_kernel = 0x88800000;

    TEST_ASSERT(mem.Translate(addr_user) == mem.Translate(addr_uncached));
    TEST_ASSERT(mem.Translate(addr_user) == mem.Translate(addr_kernel));

    mem.WriteU32(addr_user, 0x12345678);
    TEST_ASSERT(mem.ReadU32(addr_uncached) == 0x12345678);
    TEST_ASSERT(mem.ReadU32(addr_kernel) == 0x12345678);

    // 3. Little-endian scalar reads and writes
    mem.WriteU8(addr_user, 0xAB);
    TEST_ASSERT(mem.ReadU8(addr_user) == 0xAB);

    mem.WriteU16(addr_user, 0xCDEF);
    TEST_ASSERT(mem.ReadU16(addr_user) == 0xCDEF);
    TEST_ASSERT(mem.ReadU8(addr_user) == 0xEF);
    TEST_ASSERT(mem.ReadU8(addr_user + 1) == 0xCD);

    mem.WriteU32(addr_user, 0x89ABCDEF);
    TEST_ASSERT(mem.ReadU32(addr_user) == 0x89ABCDEF);
    TEST_ASSERT(mem.ReadU8(addr_user) == 0xEF);
    TEST_ASSERT(mem.ReadU8(addr_user + 1) == 0xCD);
    TEST_ASSERT(mem.ReadU8(addr_user + 2) == 0xAB);
    TEST_ASSERT(mem.ReadU8(addr_user + 3) == 0x89);

    mem.WriteU64(addr_user, 0x0123456789ABCDEFULL);
    TEST_ASSERT(mem.ReadU64(addr_user) == 0x0123456789ABCDEFULL);

    // 4. Block copy
    const char test_str[] = "Hello PSP Guest Memory";
    mem.WriteBlock(addr_user + 0x100, test_str, sizeof(test_str));
    char read_buf[sizeof(test_str)];
    mem.ReadBlock(addr_user + 0x100, read_buf, sizeof(test_str));
    TEST_ASSERT(std::memcmp(test_str, read_buf, sizeof(test_str)) == 0);

    // 5. QueryRegion and IsValidAddress
    TEST_ASSERT(mem.QueryRegion(GuestMemory::kScratchpadAddress) == GuestMemoryRegion::kScratchpad);
    TEST_ASSERT(mem.QueryRegion(GuestMemory::kVramAddress) == GuestMemoryRegion::kVram);
    TEST_ASSERT(mem.QueryRegion(GuestMemory::kKernelRamAddress) == GuestMemoryRegion::kKernelRam);
    TEST_ASSERT(mem.QueryRegion(GuestMemory::kUserRamAddress) == GuestMemoryRegion::kUserRam);
    TEST_ASSERT(mem.QueryRegion(0x00000000) == GuestMemoryRegion::kInvalid);
    TEST_ASSERT(mem.QueryRegion(0x05000000) == GuestMemoryRegion::kInvalid);

    TEST_ASSERT(mem.IsValidAddress(addr_user, 1024));
    TEST_ASSERT(!mem.IsValidAddress(0x00000000, 4));
    TEST_ASSERT(!mem.IsValidAddress(0x00010000 + static_cast<uint32_t>(GuestMemory::kScratchpadSize) - 2, 4));

    // 6. Out-of-region read inside the 512 MiB reserved window caught by GuardedRun
    repsp::platform::GuardedRunResult oob_res = repsp::platform::GuardedRun([&]()
    {
        volatile uint32_t val = mem.ReadU32(0x1FFFFFFC);
        (void)val;
    });
    TEST_ASSERT(!oob_res.succeeded);
    TEST_ASSERT(oob_res.fault_address != 0);

    mem.Shutdown();
    std::printf("memory_probe passed.\n");
    return 0;
}
