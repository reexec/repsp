#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/sha1.h"
#include "repsp/runtime/user_partition.h"

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

namespace
{

struct FakeCpuState
{
    uint32_t gpr[32] = {};
};

FakeCpuState g_cpu;

void FakeSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void FakeRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t FakeGetGpr(int idx) { return (idx >= 0 && idx < 32) ? g_cpu.gpr[idx] : 0; }
void FakeSetGpr(int idx, uint32_t v) { if (idx > 0 && idx < 32) g_cpu.gpr[idx] = v; }
void FakeCallGuest(uint32_t) {}

const repsp_guest_cpu_ops g_fake_ops = {
    sizeof(FakeCpuState), FakeSave, FakeRestore, FakeGetGpr, FakeSetGpr, FakeCallGuest, nullptr, nullptr
};

int32_t CallAlloc(int32_t part, uint32_t name_addr, uint32_t type, uint32_t size, uint32_t addr)
{
    g_cpu.gpr[4] = static_cast<uint32_t>(part);
    g_cpu.gpr[5] = name_addr;
    g_cpu.gpr[6] = type;
    g_cpu.gpr[7] = size;
    g_cpu.gpr[8] = addr;
    repsp_recomp_hle_call(repsp::runtime::ComputeNid("sceKernelAllocPartitionMemory"));
    return static_cast<int32_t>(g_cpu.gpr[2]);
}

int32_t CallFree(int32_t uid)
{
    g_cpu.gpr[4] = static_cast<uint32_t>(uid);
    repsp_recomp_hle_call(repsp::runtime::ComputeNid("sceKernelFreePartitionMemory"));
    return static_cast<int32_t>(g_cpu.gpr[2]);
}

uint32_t CallGetHeadAddr(int32_t uid)
{
    g_cpu.gpr[4] = static_cast<uint32_t>(uid);
    repsp_recomp_hle_call(repsp::runtime::ComputeNid("sceKernelGetBlockHeadAddr"));
    return g_cpu.gpr[2];
}

} // namespace

int main()
{
    using namespace repsp::runtime;
    using namespace repsp::hle;

    TEST_ASSERT(GetGuestMemory().Initialize());
    repsp_recomp_register_cpu(&g_fake_ops);

    kernel::KernelConfig cfg;
    cfg.module_image_base = 0x08804000u;
    cfg.module_image_size = 0x00100000u;
    kernel::Register(cfg);

    // Put block name strings at 0x09000000
    const uint32_t name_addr = 0x09000000u;
    std::memcpy(GetGuestMemory().Translate(name_addr), "test_blk", 9);

    // 1. Refuse partition != 2
    int32_t bad_part = CallAlloc(1, name_addr, 0, 4096, 0);
    TEST_ASSERT(bad_part == kSceKernelErrorIllegalPartitionId);

    // 2. Low allocation starts after reserved module image (0x08904000)
    int32_t uid_low = CallAlloc(2, name_addr, 0 /*Low*/, 4096, 0);
    TEST_ASSERT(uid_low > 0);
    uint32_t addr_low = CallGetHeadAddr(uid_low);
    TEST_ASSERT(addr_low == 0x08904000u);

    // 3. High allocation lands at top of 24 MiB user partition (0x0A000000 - 4096)
    int32_t uid_high = CallAlloc(2, name_addr, 1 /*High*/, 4096, 0);
    TEST_ASSERT(uid_high > 0);
    uint32_t addr_high = CallGetHeadAddr(uid_high);
    TEST_ASSERT(addr_high == 0x0A000000u - 4096u);

    // 4. Fixed-address allocation succeeds when free and refuses overlap
    const uint32_t fixed_addr = 0x09200000u;
    int32_t uid_fixed = CallAlloc(2, name_addr, 2 /*Addr*/, 8192, fixed_addr);
    TEST_ASSERT(uid_fixed > 0);
    TEST_ASSERT(CallGetHeadAddr(uid_fixed) == fixed_addr);

    int32_t overlap = CallAlloc(2, name_addr, 2 /*Addr*/, 4096, fixed_addr + 4096u);
    TEST_ASSERT(overlap == kSceKernelErrorNoMemory);

    // 5. Free and reuse
    TEST_ASSERT(CallFree(uid_low) == 0);
    int32_t uid_low2 = CallAlloc(2, name_addr, 0 /*Low*/, 4096, 0);
    TEST_ASSERT(uid_low2 > 0);
    TEST_ASSERT(CallGetHeadAddr(uid_low2) == 0x08904000u);

    TEST_ASSERT(CallFree(uid_low2) == 0);
    TEST_ASSERT(CallFree(uid_high) == 0);
    TEST_ASSERT(CallFree(uid_fixed) == 0);

    std::printf("sysmem_probe passed.\n");
    return 0;
}
