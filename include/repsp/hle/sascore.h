#ifndef REPSP_HLE_SASCORE_H
#define REPSP_HLE_SASCORE_H

#include <cstdint>
#include <string>

namespace repsp::hle::sascore
{

constexpr int32_t kSasMaxVoices = 32;
constexpr int32_t kSasMinGrain = 32;
constexpr int32_t kSasMaxGrain = 2048;
constexpr int32_t kSasDefaultSampleRate = 44100;
constexpr int32_t kSasPitchBase = 0x1000;
constexpr int32_t kSasPitchMax = 0x4000;
constexpr int32_t kSasVolumeMax = 0x1000;
constexpr int32_t kSasOutputModeInterleaved = 0;
constexpr int32_t kSasOutputModeRaw = 1;

struct SasStats
{
    uint32_t instances = 0;
    uint64_t core_calls = 0;
    uint64_t frames_synthesized = 0;
    uint64_t key_ons = 0;
    uint64_t key_offs = 0;
    uint64_t voices_set = 0;
    uint32_t active_voices = 0;
    uint32_t end_flags = 0xFFFFFFFFu;
    int32_t reverb_type = 0;
    bool reverb_dry = true;
    bool reverb_wet = false;
    bool reverb_applied = false;
};

// Register all 13 sceSasCore NIDs in the runtime NID registry.
void Register();
void Shutdown();

SasStats GetStats();
std::string FormatStats();

} // namespace repsp::hle::sascore

#endif // REPSP_HLE_SASCORE_H
