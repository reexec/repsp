#include "repsp/platform/guarded_run.h"
#include "repsp/platform/host_thread.h"
#include "repsp/platform/path.h"
#include "repsp/platform/platform.h"
#include "repsp/platform/virtual_memory.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
    using namespace repsp::platform;

    // 1. Virtual memory reservation and commit
    const size_t kPageSize = 4096;
    const size_t kReserveSize = 64 * 1024; // 64 KiB
    void* reserved = ReserveAddressSpace(kReserveSize);
    TEST_ASSERT(reserved != nullptr);

    bool committed = CommitMemory(reserved, kPageSize, MemoryProtection::kReadWrite);
    TEST_ASSERT(committed);

    auto* byte_ptr = static_cast<volatile uint8_t*>(reserved);
    byte_ptr[0] = 0x5A;
    TEST_ASSERT(byte_ptr[0] == 0x5A);

    // 2. Guarded run catching bad access (uncommitted page in reserved window)
    GuardedRunResult good_result = GuardedRun([byte_ptr]()
    {
        volatile uint8_t val = byte_ptr[0];
        (void)val;
    });
    TEST_ASSERT(good_result.succeeded);

    // Access uncommitted page at offset 32 KiB
    volatile uint8_t* uncommitted_ptr = byte_ptr + 32 * 1024;
    GuardedRunResult crash_result = GuardedRun([uncommitted_ptr]()
    {
        volatile uint8_t bad_val = *uncommitted_ptr;
        (void)bad_val;
    });
    TEST_ASSERT(!crash_result.succeeded);
    TEST_ASSERT(crash_result.fault_address != 0);

    TEST_ASSERT(ReleaseAddressSpace(reserved, kReserveSize));

    // 3. Host thread
    std::atomic<bool> thread_ran{false};
    {
        HostThread thread("TestWorkerThread", [&thread_ran]()
        {
            HostThread::SetCurrentThreadName("TestWorkerThread");
            thread_ran = true;
        });

        thread.Join();
        TEST_ASSERT(thread_ran.load());
        TEST_ASSERT(thread.GetName() == "TestWorkerThread");
        uint64_t cpu_time = thread.GetCpuTimeNs();
        (void)cpu_time;
    }

    // 4. Executable and user data paths
    auto exe_dir = GetExecutableDirectory();
    TEST_ASSERT(!exe_dir.empty());
    auto user_dir = GetUserDataDirectory("repsp_test");
    TEST_ASSERT(!user_dir.empty());

    // 5. SDL3 dummy initialization and shutdown
    PlatformConfig plat_config;
    plat_config.headless = true;
    plat_config.display.dummy = true;
    plat_config.audio.dummy = true;

    TEST_ASSERT(Initialize(plat_config));
    TEST_ASSERT(GetDisplay() != nullptr);
    TEST_ASSERT(GetInput() != nullptr);
    TEST_ASSERT(GetAudio() != nullptr);
    Shutdown();

    std::printf("platform_probe passed.\n");
    return 0;
}
