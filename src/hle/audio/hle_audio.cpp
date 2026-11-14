#include "repsp/hle/audio.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/platform/audio.h"
#include "repsp/platform/platform.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <vector>

namespace repsp::hle::audio
{

namespace
{

struct StereoFrame
{
    int16_t left = 0;
    int16_t right = 0;
};

struct AudioChannelState
{
    bool reserved = false;
    int32_t sample_count = 0;
    int32_t format = kAudioFormatStereo;
    std::deque<StereoFrame> queue;
    bool wait_active = false;
    bool wait_for_zero = false;
    bool flushing_null = false;
    uint64_t output_calls = 0;
    uint64_t frames_written = 0;
};

std::mutex g_audio_mutex;
bool g_initialized = false;
platform::AudioBackend* g_backend = nullptr;
std::array<AudioChannelState, kAudioMaxChannels> g_channels{};
uint64_t g_total_output_calls = 0;
uint64_t g_total_frames_written = 0;
uint64_t g_total_frames_pulled = 0;
uint64_t g_underruns = 0;

bool IsValidSampleCount(int32_t sample_count)
{
    return sample_count >= kAudioMinSamples &&
           sample_count <= kAudioMaxSamples &&
           (sample_count % 64) == 0;
}

bool IsValidVolume(int32_t volume)
{
    return volume >= 0 && volume <= 0xFFFF;
}

int16_t ClampI16(int32_t v)
{
    return static_cast<int16_t>(std::clamp(v, -32768, 32767));
}

int16_t ScaleSample(int16_t sample, int32_t volume)
{
    const int32_t clamped_vol = std::min<int32_t>(volume, 0x8000);
    return ClampI16((static_cast<int32_t>(sample) * clamped_vol) >> 15);
}

uint16_t ReadLeU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

int16_t ReadLeI16(const uint8_t* p)
{
    return static_cast<int16_t>(ReadLeU16(p));
}

bool WaitConditionMetLocked(const AudioChannelState& ch, bool wait_for_zero)
{
    if (!ch.reserved)
    {
        return true;
    }
    if (wait_for_zero)
    {
        return ch.queue.empty();
    }
    // Double-buffered capacity: allow one playing block plus one queued block.
    return ch.queue.size() <= static_cast<size_t>(ch.sample_count);
}

int32_t OutputChannelCommon(int32_t channel, int32_t left_vol, int32_t right_vol, uint32_t buf_addr)
{
    if (channel < 0 || channel >= kAudioMaxChannels)
    {
        return kSceAudioErrorInvalidCh;
    }
    if (!IsValidVolume(left_vol) || !IsValidVolume(right_vol))
    {
        return kSceAudioErrorInvalidVolume;
    }

    const bool wait_for_zero = (buf_addr == 0);
    int32_t sample_count = 0;

    {
        std::unique_lock<std::mutex> lock(g_audio_mutex);
        auto& ch = g_channels[static_cast<size_t>(channel)];
        if (!ch.reserved)
        {
            return kSceAudioErrorNotReserved;
        }
        if (ch.wait_active)
        {
            return kSceAudioErrorOutputBusy;
        }

        while (!WaitConditionMetLocked(ch, wait_for_zero))
        {
            ch.wait_active = true;
            ch.wait_for_zero = wait_for_zero;
            if (wait_for_zero)
            {
                ch.flushing_null = true;
            }
            lock.unlock();

            auto& sched = runtime::GetScheduler();
            sched.AddExternalWaitSource(1);
            const int32_t wait_res = sched.WaitCurrent(
                runtime::WaitType::kAudio,
                static_cast<runtime::KernelUid>(channel),
                wait_for_zero ? "AudioDrain" : "AudioOutput",
                nullptr,
                false);
            sched.RemoveExternalWaitSource(1);

            lock.lock();
            ch.wait_active = false;
            ch.wait_for_zero = false;
            ch.flushing_null = false;

            if (!g_initialized || !ch.reserved || wait_res == runtime::Scheduler::kWaitDeleted)
            {
                return wait_for_zero ? 0 : kSceAudioErrorNotReserved;
            }
            if (wait_res != runtime::Scheduler::kWaitOk)
            {
                return wait_res;
            }
        }

        if (wait_for_zero)
        {
            ch.flushing_null = false;
            ++ch.output_calls;
            ++g_total_output_calls;
            return 0;
        }

        sample_count = ch.sample_count;
        const int32_t format = ch.format;
        const size_t stride_bytes = (format == kAudioFormatMono) ? sizeof(int16_t) : (2u * sizeof(int16_t));
        const size_t total_bytes = static_cast<size_t>(sample_count) * stride_bytes;
        const uint8_t* guest_pcm = runtime::GetGuestMemory().Translate(buf_addr);
        if (guest_pcm == nullptr || (total_bytes > 0 && runtime::GetGuestMemory().Translate(buf_addr + static_cast<uint32_t>(total_bytes - 1u)) == nullptr))
        {
            return kSceKernelErrorInvalidPointer;
        }

        for (int32_t i = 0; i < sample_count; ++i)
        {
            const uint8_t* frame_ptr = guest_pcm + static_cast<size_t>(i) * stride_bytes;
            const int16_t src_l = ReadLeI16(frame_ptr);
            const int16_t src_r = (format == kAudioFormatMono) ? src_l : ReadLeI16(frame_ptr + 2);
            StereoFrame f;
            f.left = ScaleSample(src_l, left_vol);
            f.right = ScaleSample(src_r, right_vol);
            ch.queue.push_back(f);
        }

        ch.flushing_null = false;
        ++ch.output_calls;
        ch.frames_written += static_cast<uint64_t>(sample_count);
        ++g_total_output_calls;
        g_total_frames_written += static_cast<uint64_t>(sample_count);
    }

    return sample_count;
}

void HleAudioChReserve()
{
    const int32_t channel = recomp::Arg<int32_t>(0);
    const int32_t sample_count = recomp::Arg<int32_t>(1);
    const int32_t format = recomp::Arg<int32_t>(2);

    if (!IsValidSampleCount(sample_count))
    {
        recomp::Return(kSceAudioErrorInvalidSize);
        return;
    }
    if (format != kAudioFormatStereo && format != kAudioFormatMono)
    {
        recomp::Return(kSceAudioErrorInvalidFormat);
        return;
    }

    std::lock_guard<std::mutex> lock(g_audio_mutex);
    int32_t assigned = channel;
    if (channel == kAudioNextChannel)
    {
        assigned = -1;
        for (int32_t i = 0; i < kAudioMaxChannels; ++i)
        {
            if (!g_channels[static_cast<size_t>(i)].reserved)
            {
                assigned = i;
                break;
            }
        }
        if (assigned < 0)
        {
            recomp::Return(kSceAudioErrorNotFound);
            return;
        }
    }
    else if (channel < 0 || channel >= kAudioMaxChannels)
    {
        recomp::Return(kSceAudioErrorInvalidCh);
        return;
    }
    else if (g_channels[static_cast<size_t>(channel)].reserved)
    {
        recomp::Return(kSceAudioErrorOutputBusy);
        return;
    }

    auto& ch = g_channels[static_cast<size_t>(assigned)];
    ch = AudioChannelState{};
    ch.reserved = true;
    ch.sample_count = sample_count;
    ch.format = format;

    runtime::Log(
        runtime::LogLevel::kDebug,
        "[sceAudio] ChReserve -> ch=%d samplecount=%d format=0x%X",
        assigned,
        sample_count,
        format);
    recomp::Return(assigned);
}

void HleAudioChRelease()
{
    const int32_t channel = recomp::Arg<int32_t>(0);
    if (channel < 0 || channel >= kAudioMaxChannels)
    {
        recomp::Return(kSceAudioErrorInvalidCh);
        return;
    }

    bool had_waiter = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        auto& ch = g_channels[static_cast<size_t>(channel)];
        if (!ch.reserved)
        {
            recomp::Return(kSceAudioErrorNotReserved);
            return;
        }
        had_waiter = ch.wait_active;
        ch.reserved = false;
        ch.queue.clear();
        ch.wait_active = false;
        ch.wait_for_zero = false;
        ch.flushing_null = false;
    }

    if (had_waiter)
    {
        runtime::GetScheduler().WakeAllWaiters(
            runtime::WaitType::kAudio,
            static_cast<runtime::KernelUid>(channel),
            runtime::Scheduler::kWaitDeleted);
    }

    runtime::Log(runtime::LogLevel::kDebug, "[sceAudio] ChRelease(ch=%d)", channel);
    recomp::Return(0);
}

void HleAudioOutputBlocking()
{
    const int32_t channel = recomp::Arg<int32_t>(0);
    const int32_t volume = recomp::Arg<int32_t>(1);
    const uint32_t buf_addr = recomp::Arg<uint32_t>(2);
    recomp::Return(OutputChannelCommon(channel, volume, volume, buf_addr));
}

void HleAudioOutputPannedBlocking()
{
    const int32_t channel = recomp::Arg<int32_t>(0);
    const int32_t left_vol = recomp::Arg<int32_t>(1);
    const int32_t right_vol = recomp::Arg<int32_t>(2);
    const uint32_t buf_addr = recomp::Arg<uint32_t>(3);
    recomp::Return(OutputChannelCommon(channel, left_vol, right_vol, buf_addr));
}

void HleAudioGetChannelRestLength()
{
    const int32_t channel = recomp::Arg<int32_t>(0);
    if (channel < 0 || channel >= kAudioMaxChannels)
    {
        recomp::Return(kSceAudioErrorInvalidCh);
        return;
    }

    std::lock_guard<std::mutex> lock(g_audio_mutex);
    const auto& ch = g_channels[static_cast<size_t>(channel)];
    if (!ch.reserved)
    {
        recomp::Return(kSceAudioErrorNotReserved);
        return;
    }
    recomp::Return(static_cast<int32_t>(ch.queue.size()));
}

void HleAudioSetChannelDataLen()
{
    const int32_t channel = recomp::Arg<int32_t>(0);
    const int32_t sample_count = recomp::Arg<int32_t>(1);
    if (channel < 0 || channel >= kAudioMaxChannels)
    {
        recomp::Return(kSceAudioErrorInvalidCh);
        return;
    }
    if (!IsValidSampleCount(sample_count))
    {
        recomp::Return(kSceAudioErrorInvalidSize);
        return;
    }

    std::lock_guard<std::mutex> lock(g_audio_mutex);
    auto& ch = g_channels[static_cast<size_t>(channel)];
    if (!ch.reserved)
    {
        recomp::Return(kSceAudioErrorNotReserved);
        return;
    }
    ch.sample_count = sample_count;
    recomp::Return(0);
}

void AttachBackendPull(platform::AudioBackend* backend)
{
    if (backend == nullptr)
    {
        return;
    }
    backend->SetPullCallback([](int16_t* stereo_out, size_t frame_count)
    {
        MixPull(stereo_out, frame_count);
    });
}

} // namespace

void MixPull(int16_t* stereo_out, size_t frame_count)
{
    if (stereo_out == nullptr || frame_count == 0)
    {
        return;
    }
    std::memset(stereo_out, 0, frame_count * 2u * sizeof(int16_t));

    std::array<bool, kAudioMaxChannels> to_wake{};
    bool any_to_wake = false;

    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        if (!g_initialized)
        {
            return;
        }

        std::array<size_t, kAudioMaxChannels> initial_depth{};
        for (size_t c = 0; c < static_cast<size_t>(kAudioMaxChannels); ++c)
        {
            initial_depth[c] = g_channels[c].queue.size();
        }

        for (size_t f = 0; f < frame_count; ++f)
        {
            int32_t sum_l = 0;
            int32_t sum_r = 0;
            for (size_t c = 0; c < static_cast<size_t>(kAudioMaxChannels); ++c)
            {
                auto& ch = g_channels[c];
                if (!ch.reserved || ch.queue.empty())
                {
                    continue;
                }
                const StereoFrame s = ch.queue.front();
                ch.queue.pop_front();
                sum_l += s.left;
                sum_r += s.right;
            }
            stereo_out[f * 2u] = ClampI16(sum_l);
            stereo_out[f * 2u + 1u] = ClampI16(sum_r);
        }

        g_total_frames_pulled += static_cast<uint64_t>(frame_count);

        for (size_t c = 0; c < static_cast<size_t>(kAudioMaxChannels); ++c)
        {
            const auto& ch = g_channels[c];
            if (initial_depth[c] > 0 && initial_depth[c] < frame_count && !ch.flushing_null && ch.queue.empty())
            {
                ++g_underruns;
            }
            if (ch.reserved && ch.wait_active && WaitConditionMetLocked(ch, ch.wait_for_zero))
            {
                to_wake[c] = true;
                any_to_wake = true;
            }
        }
    }

    if (any_to_wake)
    {
        auto& sched = runtime::GetScheduler();
        for (size_t c = 0; c < static_cast<size_t>(kAudioMaxChannels); ++c)
        {
            if (to_wake[c])
            {
                sched.WakeAllWaiters(
                    runtime::WaitType::kAudio,
                    static_cast<runtime::KernelUid>(c),
                    runtime::Scheduler::kWaitOk);
            }
        }
    }
}

void Register(const AudioHleConfig& config)
{
    Shutdown();

    platform::AudioBackend* backend = config.auto_attach_platform ? platform::GetAudio() : nullptr;
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        g_channels.fill(AudioChannelState{});
        g_total_output_calls = 0;
        g_total_frames_written = 0;
        g_total_frames_pulled = 0;
        g_underruns = 0;
        g_backend = backend;
        g_initialized = true;
    }

    AttachBackendPull(backend);

    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceAudio", "sceAudioChReserve", HleAudioChReserve);
    reg.RegisterByName("sceAudio", "sceAudioChRelease", HleAudioChRelease);
    reg.RegisterByName("sceAudio", "sceAudioOutputBlocking", HleAudioOutputBlocking);
    reg.RegisterByName("sceAudio", "sceAudioOutputPannedBlocking", HleAudioOutputPannedBlocking);
    reg.RegisterByName("sceAudio", "sceAudioGetChannelRestLength", HleAudioGetChannelRestLength);
    reg.RegisterByName("sceAudio", "sceAudioSetChannelDataLen", HleAudioSetChannelDataLen);

    runtime::Log(runtime::LogLevel::kInfo, "[sceAudio] Registered 6 sceAudio NIDs");
}

void Shutdown()
{
    platform::AudioBackend* old_backend = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        old_backend = g_backend;
        g_backend = nullptr;
        g_initialized = false;
        g_channels.fill(AudioChannelState{});
    }

    if (old_backend != nullptr)
    {
        old_backend->SetPullCallback(nullptr);
    }

    auto& sched = runtime::GetScheduler();
    for (int32_t c = 0; c < kAudioMaxChannels; ++c)
    {
        sched.WakeAllWaiters(
            runtime::WaitType::kAudio,
            static_cast<runtime::KernelUid>(c),
            runtime::Scheduler::kWaitDeleted);
    }
}

void SetBackend(platform::AudioBackend* backend)
{
    platform::AudioBackend* old_backend = nullptr;
    bool initialized = false;
    {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        old_backend = g_backend;
        g_backend = backend;
        initialized = g_initialized;
    }

    if (old_backend != nullptr && old_backend != backend)
    {
        old_backend->SetPullCallback(nullptr);
    }
    if (initialized && backend != nullptr)
    {
        AttachBackendPull(backend);
    }
}

AudioStats GetStats()
{
    std::lock_guard<std::mutex> lock(g_audio_mutex);
    AudioStats stats;
    for (size_t i = 0; i < static_cast<size_t>(kAudioMaxChannels); ++i)
    {
        const auto& src = g_channels[i];
        auto& dst = stats.channels[i];
        dst.reserved = src.reserved;
        dst.sample_count = src.sample_count;
        dst.format = src.format;
        dst.rest_length = static_cast<uint32_t>(src.queue.size());
        dst.output_calls = src.output_calls;
        dst.frames_written = src.frames_written;
        if (src.reserved)
        {
            ++stats.reserved_count;
        }
    }
    stats.total_output_calls = g_total_output_calls;
    stats.total_frames_written = g_total_frames_written;
    stats.total_frames_pulled = g_total_frames_pulled;
    stats.underruns = g_underruns;
    return stats;
}

std::string FormatStats()
{
    const AudioStats s = GetStats();
    std::ostringstream oss;
    oss << "reserved=" << s.reserved_count
        << " output_calls=" << s.total_output_calls
        << " frames_written=" << s.total_frames_written
        << " frames_pulled=" << s.total_frames_pulled
        << " underruns=" << s.underruns;
    for (int32_t i = 0; i < kAudioMaxChannels; ++i)
    {
        const auto& ch = s.channels[static_cast<size_t>(i)];
        if (ch.reserved || ch.output_calls > 0)
        {
            oss << " | ch" << i
                << "(res=" << (ch.reserved ? 1 : 0)
                << ",samples=" << ch.sample_count
                << ",fmt=" << (ch.format == kAudioFormatMono ? "mono" : "stereo")
                << ",calls=" << ch.output_calls
                << ",frames=" << ch.frames_written
                << ",rest=" << ch.rest_length << ")";
        }
    }
    return oss.str();
}

} // namespace repsp::hle::audio
