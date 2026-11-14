#ifndef REPSP_HLE_AUDIO_H
#define REPSP_HLE_AUDIO_H

#include "repsp/platform/audio.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace repsp::hle::audio
{

constexpr int32_t kAudioMaxChannels = 8;
constexpr int32_t kAudioSampleRate = 44100;
constexpr int32_t kAudioVolumeMax = 0x8000;
constexpr int32_t kAudioFormatStereo = 0x00;
constexpr int32_t kAudioFormatMono = 0x10;
constexpr int32_t kAudioNextChannel = -1;
constexpr int32_t kAudioMinSamples = 64;
constexpr int32_t kAudioMaxSamples = 65472;

struct AudioHleConfig
{
    bool auto_attach_platform = true;
};

struct ChannelStats
{
    bool reserved = false;
    int32_t sample_count = 0;
    int32_t format = kAudioFormatStereo;
    uint32_t rest_length = 0;
    uint64_t output_calls = 0;
    uint64_t frames_written = 0;
};

struct AudioStats
{
    uint32_t reserved_count = 0;
    std::array<ChannelStats, kAudioMaxChannels> channels{};
    uint64_t total_output_calls = 0;
    uint64_t total_frames_written = 0;
    uint64_t total_frames_pulled = 0;
    uint64_t underruns = 0;
};

// Register all 6 sceAudio NIDs and attach the pull mixer to the platform audio backend.
void Register(const AudioHleConfig& config = AudioHleConfig{});
void Shutdown();

// Detach or override the target platform AudioBackend (intended for unit probes).
void SetBackend(platform::AudioBackend* backend);

// Pull and mix frame_count stereo s16le frames synchronously (also invoked by the audio backend callback).
void MixPull(int16_t* stereo_out, size_t frame_count);

AudioStats GetStats();
std::string FormatStats();

} // namespace repsp::hle::audio

#endif // REPSP_HLE_AUDIO_H
