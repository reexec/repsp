#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/sha1.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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

void WriteGuestStr(uint32_t addr, const std::string& s)
{
    std::memcpy(repsp::runtime::GetGuestMemory().Translate(addr), s.c_str(), s.size() + 1);
}

int32_t CallHle3(const char* name, uint32_t a0, uint32_t a1, uint32_t a2)
{
    g_cpu.gpr[4] = a0;
    g_cpu.gpr[5] = a1;
    g_cpu.gpr[6] = a2;
    repsp_recomp_hle_call(repsp::runtime::ComputeNid(name));
    return static_cast<int32_t>(g_cpu.gpr[2]);
}

int64_t CallLseek(int32_t fd, int64_t off, int32_t whence)
{
    g_cpu.gpr[4] = static_cast<uint32_t>(fd);
    g_cpu.gpr[5] = 0; // padding
    g_cpu.gpr[6] = static_cast<uint32_t>(static_cast<uint64_t>(off) & 0xFFFFFFFFu);
    g_cpu.gpr[7] = static_cast<uint32_t>(static_cast<uint64_t>(off) >> 32);
    g_cpu.gpr[8] = static_cast<uint32_t>(whence);
    repsp_recomp_hle_call(repsp::runtime::ComputeNid("sceIoLseek"));
    uint64_t res = static_cast<uint64_t>(g_cpu.gpr[2]) | (static_cast<uint64_t>(g_cpu.gpr[3]) << 32);
    return static_cast<int64_t>(res);
}

} // namespace

int main()
{
    using namespace repsp::runtime;
    using namespace repsp::hle;

    TEST_ASSERT(GetGuestMemory().Initialize());
    repsp_recomp_register_cpu(&g_fake_ops);

    std::filesystem::path tmp_root = std::filesystem::temp_directory_path() / "repsp_iofile_probe";
    std::error_code ec;
    std::filesystem::remove_all(tmp_root, ec);
    std::filesystem::create_directories(tmp_root / "disc" / "PSP_GAME" / "USRDIR", ec);
    std::filesystem::create_directories(tmp_root / "ms", ec);

    {
        std::ofstream f(tmp_root / "disc" / "PSP_GAME" / "USRDIR" / "DATA.BIN", std::ios::binary);
        f << "HELLO_PSP_VFS!";
    }

    kernel::KernelConfig cfg;
    cfg.mounts = {
        { "disc0", tmp_root / "disc", true },
        { "ms0", tmp_root / "ms", false },
    };
    kernel::Register(cfg);

    const uint32_t path_addr = 0x09000000u;
    const uint32_t buf_addr = 0x09001000u;
    const uint32_t res_addr = 0x09002000u;

    // 1. Case-insensitive open + read + seek + close
    WriteGuestStr(path_addr, "disc0:/psp_game/usrdir/data.bin");
    int32_t fd = CallHle3("sceIoOpen", path_addr, 0x0001 /*RDONLY*/, 0);
    TEST_ASSERT(fd >= 3);

    int32_t nread = CallHle3("sceIoRead", static_cast<uint32_t>(fd), buf_addr, 5);
    TEST_ASSERT(nread == 5);
    TEST_ASSERT(std::memcmp(GetGuestMemory().Translate(buf_addr), "HELLO", 5) == 0);

    int64_t pos = CallLseek(fd, 6, 0 /*SEEK_SET*/);
    TEST_ASSERT(pos == 6);
    nread = CallHle3("sceIoRead", static_cast<uint32_t>(fd), buf_addr, 8);
    TEST_ASSERT(nread == 8);
    TEST_ASSERT(std::memcmp(GetGuestMemory().Translate(buf_addr), "PSP_VFS!", 8) == 0);

    TEST_ASSERT(CallHle3("sceIoClose", static_cast<uint32_t>(fd), 0, 0) == 0);

    // 2. Refuse write on read-only device
    WriteGuestStr(path_addr, "disc0:/new.bin");
    int32_t ro_err = CallHle3("sceIoOpen", path_addr, 0x0202 /*WRONLY|CREAT*/, 0777);
    TEST_ASSERT(ro_err == kSceKernelErrorErrnoEacces);

    // 3. Refuse path escaping device root
    WriteGuestStr(path_addr, "disc0:/PSP_GAME/../../secret.txt");
    int32_t esc_err = CallHle3("sceIoOpen", path_addr, 0x0001, 0);
    TEST_ASSERT(esc_err == kSceKernelErrorErrnoEnoent);

    // 4. Async open -> wait -> read -> poll + busy check
    WriteGuestStr(path_addr, "disc0:/PSP_GAME/USRDIR/DATA.BIN");
    int32_t afd = CallHle3("sceIoOpenAsync", path_addr, 0x0001, 0);
    TEST_ASSERT(afd >= 3);

    // Busy while OpenAsync result hasn't been collected
    int32_t busy = CallHle3("sceIoReadAsync", static_cast<uint32_t>(afd), buf_addr, 4);
    TEST_ASSERT(busy == kSceKernelErrorAsyncBusy);

    TEST_ASSERT(CallHle3("sceIoWaitAsync", static_cast<uint32_t>(afd), res_addr, 0) == 0);
    TEST_ASSERT(static_cast<int64_t>(GetGuestMemory().ReadU64(res_addr)) == afd);

    TEST_ASSERT(CallHle3("sceIoReadAsync", static_cast<uint32_t>(afd), buf_addr, 5) == 0);
    TEST_ASSERT(CallHle3("sceIoPollAsync", static_cast<uint32_t>(afd), res_addr, 0) == 0);
    TEST_ASSERT(static_cast<int64_t>(GetGuestMemory().ReadU64(res_addr)) == 5);
    TEST_ASSERT(std::memcmp(GetGuestMemory().Translate(buf_addr), "HELLO", 5) == 0);

    TEST_ASSERT(CallHle3("sceIoClose", static_cast<uint32_t>(afd), 0, 0) == 0);

    kernel::Register(kernel::KernelConfig{}); // closes any remaining files cleanly
    std::filesystem::remove_all(tmp_root, ec);

    std::printf("iofile_probe passed.\n");
    return 0;
}
