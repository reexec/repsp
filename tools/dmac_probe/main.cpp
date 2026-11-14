#include "repsp/hle/dmac.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define TEST_ASSERT(cond)                                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(cond))                                                                   \
        {                                                                              \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);     \
            return 1;                                                                  \
        }                                                                              \
    } while (false)

namespace
{

constexpr uint32_t kNidDmacMemcpy = 0x617F3FE6u;

struct MockCpuState
{
    uint32_t gpr[32] = {0};
    uint32_t fpr[32] = {0};
};

MockCpuState g_mock_cpu;

void MockSave(void* ctx)
{
    std::memcpy(ctx, &g_mock_cpu, sizeof(MockCpuState));
}

void MockRestore(const void* ctx)
{
    std::memcpy(&g_mock_cpu, ctx, sizeof(MockCpuState));
}

uint32_t MockGetGpr(int idx)
{
    return (idx > 0 && idx < 32) ? g_mock_cpu.gpr[idx] : 0;
}

void MockSetGpr(int idx, uint32_t val)
{
    if (idx > 0 && idx < 32)
    {
        g_mock_cpu.gpr[idx] = val;
    }
}

uint32_t MockGetFpr(int idx)
{
    return (idx >= 0 && idx < 32) ? g_mock_cpu.fpr[idx] : 0;
}

void MockSetFpr(int idx, uint32_t val)
{
    if (idx >= 0 && idx < 32)
    {
        g_mock_cpu.fpr[idx] = val;
    }
}

void MockCallGuest(uint32_t /*addr*/)
{
}

int32_t CallDmacMemcpy(uint32_t dst, uint32_t src, uint32_t size)
{
    MockSetGpr(4, dst);
    MockSetGpr(5, src);
    MockSetGpr(6, size);
    repsp_recomp_hle_call(kNidDmacMemcpy);
    return static_cast<int32_t>(MockGetGpr(2));
}

} // namespace

int main()
{
    auto& mem = repsp::runtime::GetGuestMemory();
    TEST_ASSERT(mem.Initialize());

    const repsp_guest_cpu_ops ops{
        sizeof(MockCpuState),
        MockSave,
        MockRestore,
        MockGetGpr,
        MockSetGpr,
        MockCallGuest,
        MockGetFpr,
        MockSetFpr,
    };
    repsp_recomp_register_cpu(&ops);

    repsp::hle::dmac::Register();

    // 1. User RAM -> User RAM copy + surrounding sentinel preservation
    constexpr uint32_t kSrcAddr = 0x08900000u;
    constexpr uint32_t kDstAddr = 0x08901000u;
    constexpr uint32_t kCopyLen = 64;
    mem.WriteU8(kDstAddr - 1, 0xAAu);
    mem.WriteU8(kDstAddr + kCopyLen, 0xBBu);
    for (uint32_t i = 0; i < kCopyLen; ++i)
    {
        mem.WriteU8(kSrcAddr + i, static_cast<uint8_t>((i * 7u + 3u) & 0xFFu));
        mem.WriteU8(kDstAddr + i, 0u);
    }
    TEST_ASSERT(CallDmacMemcpy(kDstAddr, kSrcAddr, kCopyLen) == 0);
    TEST_ASSERT(mem.ReadU8(kDstAddr - 1) == 0xAAu);
    TEST_ASSERT(mem.ReadU8(kDstAddr + kCopyLen) == 0xBBu);
    for (uint32_t i = 0; i < kCopyLen; ++i)
    {
        TEST_ASSERT(mem.ReadU8(kDstAddr + i) == static_cast<uint8_t>((i * 7u + 3u) & 0xFFu));
    }

    // 2. User RAM -> Uncached VRAM (0x44000000) visible via cached VRAM (0x04000000)
    constexpr uint32_t kUncachedVram = 0x44000400u;
    constexpr uint32_t kCachedVram = 0x04000400u;
    TEST_ASSERT(CallDmacMemcpy(kUncachedVram, kSrcAddr, kCopyLen) == 0);
    for (uint32_t i = 0; i < kCopyLen; ++i)
    {
        TEST_ASSERT(mem.ReadU8(kCachedVram + i) == static_cast<uint8_t>((i * 7u + 3u) & 0xFFu));
    }

    // 3. Overlapping move via cached/uncached alias
    for (uint32_t i = 0; i < 32; ++i)
    {
        mem.WriteU8(kCachedVram + i, static_cast<uint8_t>(0x10u + i));
    }
    TEST_ASSERT(CallDmacMemcpy(kUncachedVram + 8u, kCachedVram, 24u) == 0);
    for (uint32_t i = 0; i < 24u; ++i)
    {
        TEST_ASSERT(mem.ReadU8(kCachedVram + 8u + i) == static_cast<uint8_t>(0x10u + i));
    }

    // 4. Error handling: invalid size & invalid pointers
    TEST_ASSERT(CallDmacMemcpy(kDstAddr, kSrcAddr, 0) == repsp::hle::kSceKernelErrorInvalidSize);
    TEST_ASSERT(CallDmacMemcpy(0, kSrcAddr, 16) == repsp::hle::kSceKernelErrorInvalidPointer);
    TEST_ASSERT(CallDmacMemcpy(kDstAddr, 0, 16) == repsp::hle::kSceKernelErrorInvalidPointer);
    TEST_ASSERT(CallDmacMemcpy(0x12340000u, kSrcAddr, 16) == repsp::hle::kSceKernelErrorInvalidPointer);
    TEST_ASSERT(CallDmacMemcpy(kDstAddr, 0x12340000u, 16) == repsp::hle::kSceKernelErrorInvalidPointer);

    repsp::hle::dmac::Shutdown();
    mem.Shutdown();

    std::printf("[PASS] dmac_probe: 4/4 checks passed\n");
    return 0;
}
