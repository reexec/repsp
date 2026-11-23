#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/hle/umd.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/sha1.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#define TEST_ASSERT(cond)                                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(cond))                                                                   \
        {                                                                              \
            std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::exit(1);                                                              \
        }                                                                              \
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
void FakeSetGpr(int idx, uint32_t v)
{
    if (idx > 0 && idx < 32)
    {
        g_cpu.gpr[idx] = v;
    }
}

int32_t CallHle(const char* name, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0)
{
    g_cpu.gpr[4] = a0;
    g_cpu.gpr[5] = a1;
    g_cpu.gpr[6] = a2;
    repsp_recomp_hle_call(repsp::runtime::ComputeNid(name));
    return static_cast<int32_t>(g_cpu.gpr[2]);
}

constexpr uint32_t kEntryWaitWake = 0x08900100u;
constexpr uint32_t kEntryWaitTimeout = 0x08900200u;

std::atomic<int32_t> g_wait_wake_result{-1};
std::atomic<int32_t> g_wait_timeout_result{-1};

void FakeCallGuest(uint32_t entry)
{
    if (entry == kEntryWaitWake)
    {
        int32_t res = CallHle("sceUmdWaitDriveStat", repsp::hle::umd::kUmdReady);
        g_wait_wake_result.store(res);
        repsp::runtime::GetScheduler().ExitThread(0, true);
    }
    else if (entry == kEntryWaitTimeout)
    {
        int32_t res = CallHle("sceUmdWaitDriveStatWithTimer", repsp::hle::umd::kUmdReady, 40000u);
        g_wait_timeout_result.store(res);
        repsp::runtime::GetScheduler().ExitThread(0, true);
    }
}

const repsp_guest_cpu_ops g_fake_ops = {
    sizeof(FakeCpuState), FakeSave, FakeRestore, FakeGetGpr, FakeSetGpr, FakeCallGuest, nullptr, nullptr
};

void WriteGuestStr(uint32_t addr, const std::string& s)
{
    std::memcpy(repsp::runtime::GetGuestMemory().Translate(addr), s.c_str(), s.size() + 1);
}

} // namespace

int main()
{
    using namespace repsp::runtime;
    using namespace repsp::hle;

    TEST_ASSERT(GetGuestMemory().Initialize());
    repsp_recomp_register_cpu(&g_fake_ops);

    std::filesystem::path tmp_root = std::filesystem::temp_directory_path() / "repsp_umd_probe";
    std::error_code ec;
    std::filesystem::remove_all(tmp_root, ec);
    const std::filesystem::path disc_a = tmp_root / "disc_a";
    const std::filesystem::path disc_b = tmp_root / "disc_b";
    std::filesystem::create_directories(disc_a / "PSP_GAME", ec);
    std::filesystem::create_directories(disc_b / "PSP_GAME", ec);
    {
        std::ofstream f(disc_a / "PSP_GAME" / "ID.TXT", std::ios::binary);
        f << "DISC_A";
    }
    {
        std::ofstream f(disc_b / "PSP_GAME" / "ID.TXT", std::ios::binary);
        f << "DISC_B";
    }

    kernel::KernelConfig cfg;
    cfg.mounts = {
        { "disc0", disc_a, true },
        { "umd0", disc_a, true },
    };
    kernel::Register(cfg);
    GetScheduler().SetCpuOps(&g_fake_ops);

    const uint32_t path_addr = 0x09000000u;
    const uint32_t buf_addr = 0x09001000u;

    // 1. Initial state: ready, replacement prohibited.
    TEST_ASSERT(!umd::IsReplacePermitted());
    TEST_ASSERT(umd::GetDriveStat() == umd::kUmdDefaultReadyStat);
    TEST_ASSERT(CallHle("sceUmdGetDriveStat") == static_cast<int32_t>(umd::kUmdDefaultReadyStat));
    TEST_ASSERT(CallHle("sceUmdCheckMedium") == 1);

    // 2. Host eject/insert refused while prohibited.
    auto refused_eject = umd::EjectDisc();
    TEST_ASSERT(!refused_eject.ok && !refused_eject.reason.empty());
    auto refused_insert = umd::InsertDisc(disc_b);
    TEST_ASSERT(!refused_insert.ok && !refused_insert.reason.empty());

    // Open a file on disc_a before eject to test post-eject read failure.
    WriteGuestStr(path_addr, "disc0:/PSP_GAME/ID.TXT");
    int32_t stale_fd = CallHle("sceIoOpen", path_addr, 0x0001u, 0);
    TEST_ASSERT(stale_fd >= 3);
    TEST_ASSERT(CallHle("sceIoRead", static_cast<uint32_t>(stale_fd), buf_addr, 6) == 6);
    TEST_ASSERT(std::memcmp(GetGuestMemory().Translate(buf_addr), "DISC_A", 6) == 0);

    // 3. Permit replacement, eject, verify state and disc0: failure.
    TEST_ASSERT(CallHle("sceUmdReplacePermit") == 0);
    TEST_ASSERT(umd::IsReplacePermitted());
    auto eject_ok = umd::EjectDisc();
    TEST_ASSERT(eject_ok.ok);
    TEST_ASSERT(umd::GetDriveStat() == umd::kUmdNotPresent);
    TEST_ASSERT(CallHle("sceUmdCheckMedium") == 0);

    TEST_ASSERT(CallHle("sceIoOpen", path_addr, 0x0001u, 0) == kSceKernelErrorErrnoEnodev);
    TEST_ASSERT(CallHle("sceIoRead", static_cast<uint32_t>(stale_fd), buf_addr, 6) == kSceKernelErrorErrnoEnodev);
    TEST_ASSERT(CallHle("sceIoClose", static_cast<uint32_t>(stale_fd)) == 0);

    // 4. Insert disc_b with short ready delay: PRESENT|CHANGED -> READY.
    umd::SetReadyDelayMs(40u);
    auto insert_ok = umd::InsertDisc(disc_b);
    TEST_ASSERT(insert_ok.ok);
    const uint32_t immediate_stat = umd::GetDriveStat();
    TEST_ASSERT((immediate_stat & umd::kUmdPresent) != 0);
    TEST_ASSERT((immediate_stat & umd::kUmdChanged) != 0);
    TEST_ASSERT((immediate_stat & umd::kUmdReady) == 0);
    TEST_ASSERT(CallHle("sceUmdCheckMedium") == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    TEST_ASSERT(umd::GetDriveStat() == umd::kUmdDefaultReadyStat);

    // Verify disc0: now reads disc_b.
    int32_t fd_b = CallHle("sceIoOpen", path_addr, 0x0001u, 0);
    TEST_ASSERT(fd_b >= 3);
    TEST_ASSERT(CallHle("sceIoRead", static_cast<uint32_t>(fd_b), buf_addr, 6) == 6);
    TEST_ASSERT(std::memcmp(GetGuestMemory().Translate(buf_addr), "DISC_B", 6) == 0);
    TEST_ASSERT(CallHle("sceIoClose", static_cast<uint32_t>(fd_b)) == 0);

    // 5. WaitDriveStat wakes when disc becomes ready.
    TEST_ASSERT(umd::EjectDisc().ok);
    GetScheduler().AddExternalWaitSource(1);
    int32_t wake_thid = GetScheduler().CreateThread("umd_wait_wake", kEntryWaitWake, 0x20, 0x4000, 0, 0);
    TEST_ASSERT(wake_thid > 0);
    std::thread inserter([&disc_a]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        auto res = umd::InsertDisc(disc_a);
        TEST_ASSERT(res.ok);
    });
    GetScheduler().Run(static_cast<KernelUid>(wake_thid));
    inserter.join();
    GetScheduler().RemoveExternalWaitSource(1);
    TEST_ASSERT(g_wait_wake_result.load() == 0);
    TEST_ASSERT(umd::GetDriveStat() == umd::kUmdDefaultReadyStat);

    // 6. WaitDriveStatWithTimer times out while ejected.
    TEST_ASSERT(umd::EjectDisc().ok);
    int32_t timeout_thid = GetScheduler().CreateThread("umd_wait_timeout", kEntryWaitTimeout, 0x20, 0x4000, 0, 0);
    TEST_ASSERT(timeout_thid > 0);
    GetScheduler().Run(static_cast<KernelUid>(timeout_thid));
    TEST_ASSERT(g_wait_timeout_result.load() == kSceKernelErrorWaitTimeout);

    // 7. Prohibit again and verify host refusal.
    TEST_ASSERT(CallHle("sceUmdReplaceProhibit") == 0);
    TEST_ASSERT(!umd::IsReplacePermitted());
    TEST_ASSERT(!umd::InsertDisc(disc_a).ok);

    std::filesystem::remove_all(tmp_root, ec);
    GetGuestMemory().Shutdown();
    std::printf("umd_probe: PASS\n");
    return 0;
}
