#include "repsp/platform/guarded_run.h"
#include "repsp/platform/host_thread.h"
#include "repsp/platform/overlay.h"
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

    // 8. Gamepad shortcut state machine (windowless, deterministic time)
    {
        GamepadShortcutState st{};

        // Without LT+RT (or at/below 16384 threshold), Y/L3/R3 reach the game.
        {
            GamepadShortcutInput in{};
            in.left_trigger = 16384;
            in.right_trigger = 32767;
            in.y = true;
            in.l3 = true;
            in.r3 = true;
            const auto r = UpdateGamepadShortcuts(st, in, 100, true);
            TEST_ASSERT(!r.toggle_menu && !r.request_quit);
            TEST_ASSERT(!r.withhold_y && !r.withhold_l3 && !r.withhold_r3);
        }

        // LT+RT held (> 16384): Y/L3/R3 withheld even with no combo pressed.
        {
            GamepadShortcutInput in{};
            in.left_trigger = 16385;
            in.right_trigger = 16385;
            const auto r = UpdateGamepadShortcuts(st, in, 200, true);
            TEST_ASSERT(!r.toggle_menu && !r.request_quit);
            TEST_ASSERT(r.withhold_y && r.withhold_l3 && r.withhold_r3);
        }

        // LT+RT+Y toggles once per press.
        {
            GamepadShortcutInput in{};
            in.left_trigger = 20000;
            in.right_trigger = 20000;
            in.y = true;
            const auto r1 = UpdateGamepadShortcuts(st, in, 300, true);
            TEST_ASSERT(r1.toggle_menu && !r1.request_quit && r1.withhold_y);
            const auto r2 = UpdateGamepadShortcuts(st, in, 316, true);
            TEST_ASSERT(!r2.toggle_menu && !r2.request_quit && r2.withhold_y);
            in.y = false;
            const auto r3 = UpdateGamepadShortcuts(st, in, 332, true);
            TEST_ASSERT(!r3.toggle_menu);
            in.y = true;
            const auto r4 = UpdateGamepadShortcuts(st, in, 350, true);
            TEST_ASSERT(r4.toggle_menu);
            // When allow_menu is false (software renderer), LT+RT+Y does not toggle.
            in.y = false;
            (void)UpdateGamepadShortcuts(st, in, 366, false);
            in.y = true;
            const auto r5 = UpdateGamepadShortcuts(st, in, 382, false);
            TEST_ASSERT(!r5.toggle_menu && r5.withhold_y);
        }

        // LT+RT+L3+R3: under 1 s does not quit; at 1 s quits once; holding longer still once.
        {
            GamepadShortcutInput in{};
            in.left_trigger = 25000;
            in.right_trigger = 25000;
            in.l3 = true;
            in.r3 = true;
            const auto t0 = UpdateGamepadShortcuts(st, in, 1000, false);
            TEST_ASSERT(!t0.request_quit && t0.withhold_l3 && t0.withhold_r3);
            const auto t999 = UpdateGamepadShortcuts(st, in, 1999, false);
            TEST_ASSERT(!t999.request_quit);
            const auto t1000 = UpdateGamepadShortcuts(st, in, 2000, false);
            TEST_ASSERT(t1000.request_quit);
            const auto t1500 = UpdateGamepadShortcuts(st, in, 2500, false);
            TEST_ASSERT(!t1500.request_quit);
            const auto t3000 = UpdateGamepadShortcuts(st, in, 4000, false);
            TEST_ASSERT(!t3000.request_quit);

            // Releasing resets the timer; holding 1 s again fires once more.
            in.r3 = false;
            TEST_ASSERT(!UpdateGamepadShortcuts(st, in, 4100, false).request_quit);
            in.r3 = true;
            TEST_ASSERT(!UpdateGamepadShortcuts(st, in, 4200, false).request_quit);
            TEST_ASSERT(!UpdateGamepadShortcuts(st, in, 5199, false).request_quit);
            TEST_ASSERT(UpdateGamepadShortcuts(st, in, 5200, false).request_quit);
        }
    }

    std::printf("platform_probe passed.\n");
    return 0;
}
