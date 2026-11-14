#include "repsp/hle/ctrl.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/platform/input.h"
#include "repsp/platform/platform.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/vblank_clock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <utility>

namespace repsp::hle::ctrl
{

static_assert(offsetof(SceCtrlData, time_stamp) == 0, "SceCtrlData::time_stamp offset must be 0");
static_assert(offsetof(SceCtrlData, buttons) == 4, "SceCtrlData::buttons offset must be 4");
static_assert(offsetof(SceCtrlData, lx) == 8, "SceCtrlData::lx offset must be 8");
static_assert(offsetof(SceCtrlData, ly) == 9, "SceCtrlData::ly offset must be 9");
static_assert(offsetof(SceCtrlData, rsrv) == 10, "SceCtrlData::rsrv offset must be 10");

namespace
{

constexpr size_t kRingSize = 64;
constexpr uint64_t kVblankPeriodUs = 16683ull;

std::mutex g_ctrl_mutex;
bool g_initialized = false;
uint32_t g_vblank_sub_id = 0;

int32_t g_sampling_cycle = 0;
int32_t g_sampling_mode = kPspCtrlModeDigital;
std::array<SceCtrlData, kRingSize> g_ring{};
uint64_t g_samples_produced = 0;
uint64_t g_last_read_seq = 0;
uint64_t g_samples_read = 0;
InputSourceFn g_input_source;

uint8_t NormalizeAnalogAxis(float v)
{
    const float clamped = std::clamp(v, -1.0f, 1.0f);
    const long rounded = std::lround((clamped + 1.0f) * 127.5f);
    return static_cast<uint8_t>(std::clamp(rounded, 0l, 255l));
}

void ProduceSample(uint32_t vcount)
{
    InputSourceFn source;
    {
        std::lock_guard<std::mutex> lock(g_ctrl_mutex);
        if (!g_initialized)
        {
            return;
        }
        source = g_input_source;
    }

    platform::ControllerState raw{};
    if (source)
    {
        raw = source(vcount);
    }
    else if (platform::InputBackend* input = platform::GetInput())
    {
        input->Poll(raw);
    }

    std::lock_guard<std::mutex> lock(g_ctrl_mutex);
    if (!g_initialized)
    {
        return;
    }

    SceCtrlData sample{};
    const uint64_t next_seq = g_samples_produced + 1;
    sample.time_stamp = static_cast<uint32_t>(next_seq * kVblankPeriodUs);
    sample.buttons = raw.buttons;
    if (g_sampling_mode == kPspCtrlModeAnalog)
    {
        sample.lx = NormalizeAnalogAxis(raw.lx);
        sample.ly = NormalizeAnalogAxis(raw.ly);
    }
    else
    {
        sample.lx = 128;
        sample.ly = 128;
    }

    g_ring[(next_seq - 1) % kRingSize] = sample;
    g_samples_produced = next_seq;
}

void OnVblankTick(uint32_t vcount)
{
    ProduceSample(vcount);
}

// Record non-negative cycle (0 = VBlank; >0 = recorded while staying VBlank-synced).
void HleSetSamplingCycle()
{
    const int32_t cycle = static_cast<int32_t>(recomp::Arg<uint32_t>(0));
    if (cycle < 0)
    {
        recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorInvalidValue));
        return;
    }

    std::lock_guard<std::mutex> lock(g_ctrl_mutex);
    const int32_t prev = g_sampling_cycle;
    g_sampling_cycle = cycle;
    recomp::SetGpr(2, static_cast<uint32_t>(prev));
}

void HleSetSamplingMode()
{
    const int32_t mode = static_cast<int32_t>(recomp::Arg<uint32_t>(0));
    if (mode != kPspCtrlModeDigital && mode != kPspCtrlModeAnalog)
    {
        recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorInvalidMode));
        return;
    }

    std::lock_guard<std::mutex> lock(g_ctrl_mutex);
    const int32_t prev = g_sampling_mode;
    g_sampling_mode = mode;
    recomp::SetGpr(2, static_cast<uint32_t>(prev));
}

void HleReadBufferPositive()
{
    const uint32_t pad_addr = recomp::Arg<uint32_t>(0);
    const int32_t count = static_cast<int32_t>(recomp::Arg<uint32_t>(1));

    if (count <= 0 || count > static_cast<int32_t>(kRingSize))
    {
        recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorInvalidSize));
        return;
    }

    const size_t total_bytes = static_cast<size_t>(count) * sizeof(SceCtrlData);
    auto& mem = runtime::GetGuestMemory();
    if (pad_addr == 0 || !mem.IsValidAddress(pad_addr, total_bytes))
    {
        recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorInvalidPointer));
        return;
    }

    bool need_wait = false;
    {
        std::lock_guard<std::mutex> lock(g_ctrl_mutex);
        if (!g_initialized)
        {
            recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorInvalidValue));
            return;
        }
        need_wait = (g_samples_produced <= g_last_read_seq);
    }

    if (need_wait)
    {
        if (runtime::GetScheduler().GetCurrentThreadId() > 0)
        {
            const int32_t wait_res = runtime::GetVblankClock().WaitNext(false);
            if (wait_res < 0)
            {
                recomp::SetGpr(2, static_cast<uint32_t>(wait_res));
                return;
            }
        }
        else
        {
            ProduceSample(runtime::GetVblankClock().Count());
        }
    }

    std::array<SceCtrlData, kRingSize> out{};
    int32_t actual = 0;
    {
        std::lock_guard<std::mutex> lock(g_ctrl_mutex);
        if (!g_initialized)
        {
            recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorInvalidValue));
            return;
        }
        const uint64_t unread = (g_samples_produced > g_last_read_seq)
                                    ? (g_samples_produced - g_last_read_seq)
                                    : 0;
        if (unread == 0)
        {
            recomp::SetGpr(2, 0);
            return;
        }

        const uint64_t available = std::min<uint64_t>(unread, kRingSize);
        actual = static_cast<int32_t>(std::min<uint64_t>(static_cast<uint64_t>(count), available));
        const uint64_t first_seq = g_samples_produced - static_cast<uint64_t>(actual) + 1;
        for (int32_t i = 0; i < actual; ++i)
        {
            const uint64_t seq = first_seq + static_cast<uint64_t>(i);
            out[static_cast<size_t>(i)] = g_ring[(seq - 1) % kRingSize];
        }
        g_last_read_seq = g_samples_produced;
        g_samples_read += static_cast<uint64_t>(actual);
    }

    mem.WriteBlock(pad_addr, out.data(), static_cast<size_t>(actual) * sizeof(SceCtrlData));
    recomp::SetGpr(2, static_cast<uint32_t>(actual));
}

} // namespace

void Register()
{
    uint32_t old_sub_id = 0;
    bool was_initialized = false;
    {
        std::lock_guard<std::mutex> lock(g_ctrl_mutex);
        was_initialized = g_initialized;
        g_initialized = false;
        old_sub_id = g_vblank_sub_id;
        g_vblank_sub_id = 0;
    }

    if (was_initialized)
    {
        runtime::GetVblankClock().Unsubscribe(old_sub_id);
        runtime::GetVblankClock().Stop();
    }

    {
        std::lock_guard<std::mutex> lock(g_ctrl_mutex);
        g_sampling_cycle = 0;
        g_sampling_mode = kPspCtrlModeDigital;
        g_ring.fill(SceCtrlData{});
        g_samples_produced = 0;
        g_last_read_seq = 0;
        g_samples_read = 0;
        g_input_source = nullptr;
        g_initialized = true;
    }

    const uint32_t sub_id = runtime::GetVblankClock().Subscribe([](uint32_t vcount) { OnVblankTick(vcount); });
    {
        std::lock_guard<std::mutex> lock(g_ctrl_mutex);
        g_vblank_sub_id = sub_id;
    }

    runtime::GetVblankClock().Start();

    auto& reg = runtime::GetNidRegistry();
    reg.Register("sceCtrl", 0x6A2774F3u, "sceCtrlSetSamplingCycle", HleSetSamplingCycle);
    reg.Register("sceCtrl", 0x1F4011E6u, "sceCtrlSetSamplingMode", HleSetSamplingMode);
    reg.Register("sceCtrl", 0x1F803938u, "sceCtrlReadBufferPositive", HleReadBufferPositive);
}

void Shutdown()
{
    uint32_t sub_id = 0;
    bool was_initialized = false;
    {
        std::lock_guard<std::mutex> lock(g_ctrl_mutex);
        if (!g_initialized)
        {
            return;
        }
        was_initialized = true;
        g_initialized = false;
        sub_id = g_vblank_sub_id;
        g_vblank_sub_id = 0;
        g_input_source = nullptr;
    }

    if (was_initialized)
    {
        runtime::GetVblankClock().Unsubscribe(sub_id);
        runtime::GetVblankClock().Stop();
    }
}

void SetInputSource(InputSourceFn fn)
{
    std::lock_guard<std::mutex> lock(g_ctrl_mutex);
    g_input_source = std::move(fn);
}

CtrlStats GetStats()
{
    std::lock_guard<std::mutex> lock(g_ctrl_mutex);
    CtrlStats stats;
    stats.sampling_cycle = g_sampling_cycle;
    stats.sampling_mode = g_sampling_mode;
    stats.samples_produced = g_samples_produced;
    stats.samples_read = g_samples_read;
    return stats;
}

std::string FormatStats()
{
    const CtrlStats s = GetStats();
    std::ostringstream oss;
    oss << "Ctrl: cycle=" << s.sampling_cycle
        << " mode=" << s.sampling_mode
        << " produced=" << s.samples_produced
        << " read=" << s.samples_read << "\n";
    return oss.str();
}

} // namespace repsp::hle::ctrl
