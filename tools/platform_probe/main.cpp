#include "repsp/platform/guarded_run.h"
#include "repsp/platform/host_thread.h"
#include "repsp/platform/path.h"
#include "repsp/platform/platform.h"
#include "repsp/platform/virtual_memory.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

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

    // 3. Concurrent guarded calls from two threads
    bool thread_a_crashed = false;
    bool thread_b_succeeded = false;

    {
        HostThread thread_a("ThreadA", [&]()
        {
            GuardedRunResult res = GuardedRun([&]()
            {
                volatile uint8_t val = *uncommitted_ptr;
                (void)val;
            });
            thread_a_crashed = !res.succeeded && (res.fault_address != 0);
        });

        HostThread thread_b("ThreadB", [&]()
        {
            GuardedRunResult res = GuardedRun([&]()
            {
                byte_ptr[0] = 0x42;
                volatile uint8_t val = byte_ptr[0];
                (void)val;
            });
            thread_b_succeeded = res.succeeded;
        });

        thread_a.Join();
        thread_b.Join();
    }
    TEST_ASSERT(thread_a_crashed);
    TEST_ASSERT(thread_b_succeeded);

    // 4. Nested guarded calls (hardware fault)
    bool inner_crashed = false;
    GuardedRunResult outer_res = GuardedRun([&]()
    {
        GuardedRunResult inner_res = GuardedRun([&]()
        {
            volatile uint8_t val = *uncommitted_ptr;
            (void)val;
        });
        inner_crashed = !inner_res.succeeded;
    });
    TEST_ASSERT(outer_res.succeeded);
    TEST_ASSERT(inner_crashed);

    // 4b. Guarded run stop mechanism (RaiseStop / repsp_raise_stop)
    GuardedRunResult stop_res = GuardedRun([]()
    {
        repsp_raise_stop("Test explicit stop");
    });
    TEST_ASSERT(!stop_res.succeeded);
    TEST_ASSERT(stop_res.stopped);
    TEST_ASSERT(stop_res.stop_reason == "Test explicit stop");

    // 4c. Nested guarded run with RaiseStop in inner frame
    bool inner_stopped = false;
    std::string inner_reason;
    GuardedRunResult outer_stop_res = GuardedRun([&]()
    {
        GuardedRunResult inner_res = GuardedRun([]()
        {
            RaiseStop("Inner stop reason");
        });
        inner_stopped = inner_res.stopped;
        inner_reason = inner_res.stop_reason;
    });
    TEST_ASSERT(outer_stop_res.succeeded);
    TEST_ASSERT(inner_stopped);
    TEST_ASSERT(inner_reason == "Inner stop reason");

    TEST_ASSERT(ReleaseAddressSpace(reserved, kReserveSize));

    // 5. Host thread properties
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

    // 6. Executable and user data paths
    auto exe_dir = GetExecutableDirectory();
    TEST_ASSERT(!exe_dir.empty());
    auto user_dir = GetUserDataDirectory("repsp_test");
    TEST_ASSERT(!user_dir.empty());

    // 7. SDL3 dummy initialization and shutdown
    PlatformConfig plat_config;
    plat_config.headless = true;
    plat_config.display.dummy = true;
    plat_config.audio.dummy = true;

    TEST_ASSERT(Initialize(plat_config));
    TEST_ASSERT(GetDisplay() != nullptr);
    TEST_ASSERT(GetInput() != nullptr);
    {
        ControllerState st{};
        st.buttons = 0xFFFFFFFFu;
        st.lx = 0.75f;
        st.ly = -0.75f;
        GetInput()->Poll(st);
        TEST_ASSERT(st.buttons == 0u);
        TEST_ASSERT(st.lx == 0.0f);
        TEST_ASSERT(st.ly == 0.0f);
    }
    TEST_ASSERT(GetAudio() != nullptr);
    {
        std::atomic<uint64_t> pulled_frames{0};
        GetAudio()->SetPullCallback([&pulled_frames](int16_t* out, size_t frames) {
            if (out != nullptr && frames > 0)
            {
                for (size_t i = 0; i < frames * 2; ++i)
                {
                    out[i] = 0;
                }
            }
            pulled_frames.fetch_add(frames, std::memory_order_relaxed);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        GetAudio()->SetPullCallback(nullptr);
        // 60 ms at 44.1 kHz is ~2646 frames; allow wide host scheduling tolerance while proving real-time pacing.
        const uint64_t frames = pulled_frames.load(std::memory_order_relaxed);
        TEST_ASSERT(frames >= 400 && frames <= 15000);
    }
    Shutdown();
    TEST_ASSERT(GetInput() == nullptr);

    // 7. Shortcut classification and game input blocking (windowless)
    {
        TEST_ASSERT(ShouldBlockGameKeyboardInput(true, false, false));
        TEST_ASSERT(ShouldBlockGameKeyboardInput(false, true, false));
        TEST_ASSERT(ShouldBlockGameKeyboardInput(false, false, true));
        TEST_ASSERT(!ShouldBlockGameKeyboardInput(false, false, false));

        const auto tab = ClassifyShortcut(ShortcutKey::kTab, false, false, false);
        TEST_ASSERT(tab.kind == ShortcutKind::kToggleOverlay);
        TEST_ASSERT(ClassifyShortcut(ShortcutKey::kTab, false, false, true).kind == ShortcutKind::kNone);
        TEST_ASSERT(ClassifyShortcut(ShortcutKey::kTab, true, false, false).kind == ShortcutKind::kNone);

        const auto alt3 = ClassifyShortcut(ShortcutKey::kDigit3, true, false, false);
        TEST_ASSERT(alt3.kind == ShortcutKind::kWindowScale && alt3.value == 3);
        const auto alt6 = ClassifyShortcut(ShortcutKey::kDigit6, true, false, false);
        TEST_ASSERT(alt6.kind == ShortcutKind::kWindowScale && alt6.value == 6);
        TEST_ASSERT(ClassifyShortcut(ShortcutKey::kDigit7, true, false, false).kind == ShortcutKind::kNone);

        const auto alt_enter = ClassifyShortcut(ShortcutKey::kEnter, true, false, false);
        TEST_ASSERT(alt_enter.kind == ShortcutKind::kToggleFullscreen);
        TEST_ASSERT(ClassifyShortcut(ShortcutKey::kEnter, false, false, false).kind == ShortcutKind::kNone);

        const auto ctrl8 = ClassifyShortcut(ShortcutKey::kDigit8, false, true, false);
        TEST_ASSERT(ctrl8.kind == ShortcutKind::kRenderScale && ctrl8.value == 8);

        // Aspect ratio rect calculation
        const auto orig = ComputeOutputRect(1280, 720, 480, 272, AspectMode::kOriginal);
        TEST_ASSERT(orig.width > 0 && orig.height == 720 && orig.x > 0 && orig.y == 0);
        const auto stretch = ComputeOutputRect(1280, 720, 480, 272, AspectMode::kStretch);
        TEST_ASSERT(stretch.x == 0 && stretch.y == 0 && stretch.width == 1280 && stretch.height == 720);
        const auto integer = ComputeOutputRect(1280, 720, 480, 272, AspectMode::kInteger);
        TEST_ASSERT(integer.width == 960 && integer.height == 544 && integer.x == 160 && integer.y == 88);
    }

    std::printf("platform_probe passed.\n");
    return 0;
}
