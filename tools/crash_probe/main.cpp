#include "repsp/platform/crash_handler.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/platform/platform.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/exec_history.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

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

struct MockCpu
{
    uint32_t gpr[32] = {};
};

MockCpu g_cpu;
std::string g_crash_kind;

void MockSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void MockRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t MockGetGpr(int idx) { return (idx > 0 && idx < 32) ? g_cpu.gpr[idx] : 0; }
void MockSetGpr(int idx, uint32_t val) { if (idx > 0 && idx < 32) g_cpu.gpr[idx] = val; }
uint32_t MockGetFpr(int) { return 0; }
void MockSetFpr(int, uint32_t) {}

void DummyHleFn()
{
    MockSetGpr(2, 0x1234u);
}

void MockCallGuest(uint32_t entry)
{
    repsp::runtime::NoteGuestEntry(entry);
    if (g_crash_kind == "mutex_held_stop")
    {
        repsp::runtime::GetScheduler().TriggerMutexHeldStopForTest();
    }
    repsp_recomp_hle_call(0xCAFEBABEu);
    if (g_crash_kind == "guest_abort")
    {
        std::abort();
    }
}

void ExtraCrashCallback(void* /*user_data*/, repsp::platform::CrashTextBuffer* out)
{
    if (out != nullptr)
    {
        out->Append("probe_extra_tag: crash_probe_v1\n");
    }
}

[[noreturn]] void ThrowProbeException()
{
    throw std::runtime_error("probe unhandled cpp exception");
}

[[noreturn]] void ThrowUnhandledInNoexcept() noexcept
{
    ThrowProbeException();
}

int RunChild(const std::string& kind, const std::string& crash_dir)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    using WerSetFlagsFn = HRESULT(WINAPI*)(DWORD);
    if (HMODULE k32 = GetModuleHandleA("kernel32.dll"))
    {
        FARPROC proc = GetProcAddress(k32, "WerSetFlags");
        if (proc != nullptr)
        {
            WerSetFlagsFn wer_set_flags = nullptr;
            std::memcpy(&wer_set_flags, &proc, sizeof(wer_set_flags));
            // WER_FAULT_REPORTING_NO_UI (0x20) | WER_FAULT_REPORTING_NO_QUEUE (0x04)
            wer_set_flags(0x24u);
        }
    }
#endif
    g_crash_kind = kind;

    repsp::platform::CrashConfig cfg{};
    cfg.crash_dir = crash_dir;
    cfg.write_minidump = true;
    cfg.extra_callback = &ExtraCrashCallback;
    repsp::platform::InstallCrashHandler(cfg);
    repsp::runtime::RegisterRuntimeCrashProvider();

    if (kind == "access_violation")
    {
        volatile int* bad = nullptr;
        *bad = 42;
        return 0;
    }
    if (kind == "guest_abort" || kind == "mutex_held_stop")
    {
        auto& mem = repsp::runtime::GetGuestMemory();
        if (!mem.Initialize())
        {
            return 2;
        }
        repsp::platform::SetCrashGuestMemoryRange(
            reinterpret_cast<uintptr_t>(mem.Base()),
            repsp::runtime::GuestMemory::kWindowSize);

        repsp::runtime::GetNidRegistry().Register("sceProbe", 0xCAFEBABEu, "sceProbeCall", &DummyHleFn);

        repsp_guest_cpu_ops ops{};
        ops.context_size = sizeof(MockCpu);
        ops.save = &MockSave;
        ops.restore = &MockRestore;
        ops.get_gpr = &MockGetGpr;
        ops.set_gpr = &MockSetGpr;
        ops.get_fpr = &MockGetFpr;
        ops.set_fpr = &MockSetFpr;
        ops.call_guest = &MockCallGuest;

        auto& sched = repsp::runtime::GetScheduler();
        sched.Reset();
        sched.SetCpuOps(&ops);
        const char* thread_name = (kind == "mutex_held_stop") ? "mutex_held_worker" : "crash_worker";
        const uint32_t entry = (kind == "mutex_held_stop") ? 0x08802345u : 0x08801234u;
        const int32_t tid = sched.CreateThread(thread_name, entry, 0x20, 0x4000, 0, 0);
        if (tid <= 0)
        {
            return 2;
        }
        sched.Run(tid);
        return 0;
    }
    if (kind == "abort")
    {
        std::abort();
    }
    if (kind == "cpp_exception")
    {
        ThrowUnhandledInNoexcept();
    }
    if (kind == "unjoined_thread")
    {
        std::thread t([]() {});
        (void)t;
        return 0;
    }
    return 2;
}

int SpawnChildAndWait(const char* self_exe, const std::string& kind, const std::string& crash_dir)
{
#ifdef _WIN32
    std::string cmd = std::string("\"") + self_exe + "\" --child " + kind + " \"" + crash_dir + "\"";
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = INVALID_HANDLE_VALUE;
    si.hStdError = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(
            nullptr,
            cmd_buf.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi))
    {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (code == 0) ? 0 : static_cast<int>(code);
#else
    pid_t pid = fork();
    if (pid < 0)
    {
        return -1;
    }
    if (pid == 0)
    {
        const char* argv[] = {self_exe, "--child", kind.c_str(), crash_dir.c_str(), nullptr};
        execv(self_exe, const_cast<char* const*>(argv));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        return -1;
    }
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }
    return 1;
#endif
}

std::string ReadWholeFile(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 4 && std::strcmp(argv[1], "--child") == 0)
    {
        return RunChild(argv[2], argv[3]);
    }

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "repsp_crash_probe_dir";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const char* kinds[] = {
        "access_violation",
        "guest_abort",
        "abort",
        "cpp_exception",
        "unjoined_thread",
        "mutex_held_stop",
    };

    constexpr size_t kKindCount = sizeof(kinds) / sizeof(kinds[0]);
    std::vector<int> exit_codes(kKindCount, -1);
    std::vector<std::thread> workers;
    workers.reserve(kKindCount);
    for (size_t i = 0; i < kKindCount; ++i)
    {
        const std::filesystem::path dir = root / kinds[i];
        std::filesystem::create_directories(dir, ec);
        workers.emplace_back([&, i, dir]()
        {
            exit_codes[i] = SpawnChildAndWait(argv[0], kinds[i], dir.string());
        });
    }
    for (auto& w : workers)
    {
        w.join();
    }

    for (size_t i = 0; i < kKindCount; ++i)
    {
        const char* kind = kinds[i];
        const std::filesystem::path dir = root / kind;
        const int exit_code = exit_codes[i];
        TEST_ASSERT(exit_code != 0);

        bool found_txt = false;
        bool found_dmp = false;
        std::string report;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            const std::string ext = entry.path().extension().string();
            if (ext == ".txt")
            {
                found_txt = true;
                report = ReadWholeFile(entry.path());
            }
            else if (ext == ".dmp")
            {
                found_dmp = true;
            }
        }
        TEST_ASSERT(found_txt);
#ifdef _WIN32
        TEST_ASSERT(found_dmp);
#else
        (void)found_dmp;
#endif
        TEST_ASSERT(report.find("probe_extra_tag: crash_probe_v1") != std::string::npos);

        if (std::strcmp(kind, "access_violation") == 0)
        {
#ifdef _WIN32
            TEST_ASSERT(report.find("0xC0000005") != std::string::npos);
#else
            TEST_ASSERT(report.find("SIGSEGV") != std::string::npos);
#endif
        }
        else if (std::strcmp(kind, "guest_abort") == 0)
        {
            TEST_ASSERT(report.find("name=crash_worker") != std::string::npos);
            TEST_ASSERT(report.find("0x08801234") != std::string::npos);
            TEST_ASSERT(report.find("0xCAFEBABE") != std::string::npos);
        }
        else if (std::strcmp(kind, "cpp_exception") == 0)
        {
            TEST_ASSERT(report.find("probe unhandled cpp exception") != std::string::npos);
        }
        else if (std::strcmp(kind, "mutex_held_stop") == 0)
        {
            TEST_ASSERT(report.find("Stop/fault while holding scheduler lock: simulated stop while holding scheduler mutex") != std::string::npos);
            TEST_ASSERT(report.find("name=mutex_held_worker") != std::string::npos);
            // The live owner may already be another scheduler thread by the time the
            // report is written; the frozen recovery record is what must be there.
            TEST_ASSERT(report.find("scheduler_mutex_held_at_recovery: host_tid=") != std::string::npos);
        }
    }

    std::filesystem::remove_all(root, ec);
    std::printf("[PASS] crash_probe: 6/6 termination kinds verified\n");
    return 0;
}
