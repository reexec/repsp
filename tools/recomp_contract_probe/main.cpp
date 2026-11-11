#include "repsp/recomp/guest_cpu.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"

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
    uint32_t gprs[32] = {0};
    uint32_t last_called_guest = 0;
};

FakeCpuState g_fake_cpu;

struct FakeSavedContext
{
    uint32_t gprs[32];
};

void FakeSave(void* context)
{
    auto* ctx = static_cast<FakeSavedContext*>(context);
    std::memcpy(ctx->gprs, g_fake_cpu.gprs, sizeof(g_fake_cpu.gprs));
}

void FakeRestore(const void* context)
{
    const auto* ctx = static_cast<const FakeSavedContext*>(context);
    std::memcpy(g_fake_cpu.gprs, ctx->gprs, sizeof(g_fake_cpu.gprs));
}

uint32_t FakeGetGpr(int index)
{
    if (index >= 0 && index < 32)
    {
        return g_fake_cpu.gprs[index];
    }
    return 0;
}

void FakeSetGpr(int index, uint32_t value)
{
    if (index > 0 && index < 32) // $zero is hardwired to 0
    {
        g_fake_cpu.gprs[index] = value;
    }
}

void FakeCallGuest(uint32_t address)
{
    g_fake_cpu.last_called_guest = address;
}

repsp_guest_cpu_ops g_fake_ops = {
    sizeof(FakeSavedContext),
    FakeSave,
    FakeRestore,
    FakeGetGpr,
    FakeSetGpr,
    FakeCallGuest
};

} // namespace

int main()
{
    using namespace repsp::recomp;
    using namespace repsp::runtime;

    TEST_ASSERT(GetGuestMemory().Initialize());

    // 1. Register fake CPU adapter
    repsp_recomp_register_cpu(&g_fake_ops);
    TEST_ASSERT(repsp_recomp_get_cpu_ops() == &g_fake_ops);

    // 2. Test Arg(0..9) helper
    // Args 0..3: GPR 4..7 ($a0..$a3)
    g_fake_cpu.gprs[4] = 100;
    g_fake_cpu.gprs[5] = 101;
    g_fake_cpu.gprs[6] = 102;
    g_fake_cpu.gprs[7] = 103;

    // Args 4..7: GPR 8..11 ($t0..$t3)
    g_fake_cpu.gprs[8] = 104;
    g_fake_cpu.gprs[9] = 105;
    g_fake_cpu.gprs[10] = 106;
    g_fake_cpu.gprs[11] = 107;

    // Args 8..9: Stack at $sp (GPR 29)
    uint32_t test_sp = 0x08802000;
    g_fake_cpu.gprs[29] = test_sp;
    GetGuestMemory().WriteU32(test_sp + 0, 108); // Arg 8
    GetGuestMemory().WriteU32(test_sp + 4, 109); // Arg 9

    TEST_ASSERT(Arg(0) == 100);
    TEST_ASSERT(Arg(1) == 101);
    TEST_ASSERT(Arg(2) == 102);
    TEST_ASSERT(Arg(3) == 103);
    TEST_ASSERT(Arg(4) == 104);
    TEST_ASSERT(Arg(5) == 105);
    TEST_ASSERT(Arg(6) == 106);
    TEST_ASSERT(Arg(7) == 107);
    TEST_ASSERT(Arg(8) == 108);
    TEST_ASSERT(Arg(9) == 109);

    // Test allowed pointer types: char* and uint8_t*
    uint32_t str_addr = 0x08803000;
    const char hello[] = "Hello PSP";
    GetGuestMemory().WriteBlock(str_addr, hello, sizeof(hello));
    g_fake_cpu.gprs[4] = str_addr;
    const char* arg_str = Arg<const char*>(0);
    TEST_ASSERT(arg_str != nullptr);
    TEST_ASSERT(std::strcmp(arg_str, hello) == 0);

    const uint8_t* arg_bytes = Arg<const uint8_t*>(0);
    TEST_ASSERT(arg_bytes != nullptr);
    TEST_ASSERT(arg_bytes[0] == 'H');

    // Restore GPR 4 for the save/restore test
    g_fake_cpu.gprs[4] = 100;

    // 3. Test Return helper
    Return(static_cast<uint32_t>(0xCAFEBABE));
    TEST_ASSERT(g_fake_cpu.gprs[2] == 0xCAFEBABE); // $v0

    Return(static_cast<uint64_t>(0x1122334455667788ULL));
    TEST_ASSERT(g_fake_cpu.gprs[2] == 0x55667788); // $v0
    TEST_ASSERT(g_fake_cpu.gprs[3] == 0x11223344); // $v1

    // 4. Test NID calls reaching registered handlers
    bool hle_called = false;
    uint32_t my_nid = 0x12344321;
    GetNidRegistry().Register("ContractModule", my_nid, "TestHle", [&hle_called]()
    {
        hle_called = true;
    });

    repsp_recomp_hle_call(my_nid);
    TEST_ASSERT(hle_called);

    // Test unimplemented NID call setting $v0 to SCE_KERNEL_ERROR_NOTIMPLEMENTED
    repsp_recomp_hle_call(0x99999999);
    TEST_ASSERT(g_fake_cpu.gprs[2] == kSceKernelErrorNotImplemented);

    // 5. Test context save and restore
    FakeSavedContext saved_ctx;
    g_fake_ops.save(&saved_ctx);

    // Overwrite GPRs
    for (int i = 0; i < 32; ++i)
    {
        g_fake_cpu.gprs[i] = 0x55555555;
    }

    // Restore and verify original values
    g_fake_ops.restore(&saved_ctx);
    TEST_ASSERT(g_fake_cpu.gprs[4] == 100);
    TEST_ASSERT(g_fake_cpu.gprs[8] == 104);
    TEST_ASSERT(g_fake_cpu.gprs[29] == test_sp);

    // 6. Test C memory access functions
    TEST_ASSERT(repsp_guest_base() == GetGuestMemory().Base());
    TEST_ASSERT(repsp_guest_mask() == GuestMemory::kFoldMask);
    TEST_ASSERT(repsp_guest_translate(0x08800000) == GetGuestMemory().Translate(0x08800000));

    repsp_guest_write32(0x08800000, 0xAABBCCDD);
    TEST_ASSERT(repsp_guest_read32(0x08800000) == 0xAABBCCDD);

    GetGuestMemory().Shutdown();
    std::printf("recomp_contract_probe passed.\n");
    return 0;
}
