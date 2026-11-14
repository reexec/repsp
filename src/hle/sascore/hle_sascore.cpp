#include "repsp/hle/sascore.h"

#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace repsp::hle::sascore
{
namespace
{

constexpr uint32_t kVagHeaderMagic = 0x70474156u; // "VAGp" little-endian
constexpr size_t kVagHeaderSize = 48;
constexpr size_t kVagBlockBytes = 16;
constexpr int32_t kVagSamplesPerBlock = 28;
constexpr uint8_t kVagFlagLoopEnd = 0x01;
constexpr uint8_t kVagFlagLoopRepeat = 0x02;
constexpr uint8_t kVagFlagLoopStart = 0x04;
constexpr uint8_t kVagFlagEndMute = 0x07;

constexpr int32_t kSasErrorNotInitialized = static_cast<int32_t>(0x80420100u);

// Sony SPU ADPCM prediction filter coefficients (scaled by 64).
// Source: psx-spx / PSX-PSP SPU ADPCM specification.
constexpr std::array<int32_t, 5> kVagFilterK0 = { 0, 60, 115, 98, 122 };
constexpr std::array<int32_t, 5> kVagFilterK1 = { 0, 0, -52, -55, -60 };

constexpr int64_t kEnvMax = 0x40000000LL; // 1 << 30
constexpr int64_t kEnvExpThreshold = 0x30000000LL;
constexpr int32_t kSdlRateScale = 1 << 15; // Maps 15-bit SPU amplitude step into 30-bit SAS envelope scale.

enum class AdsrPhase
{
    kOff = 0,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
};

enum class AdsrCurve
{
    kLinearInc = 0,
    kLinearDec,
    kExpInc,
    kExpDec,
};

struct EnvelopeRate
{
    AdsrCurve curve = AdsrCurve::kLinearInc;
    // Fixed increment per sample in 30-bit envelope space (0x40000000 = full scale),
    // OR divider period + SPU step when sub-sample slow rates are converted from SimpleADSR.
    int64_t step_per_sample = kEnvMax;
    uint32_t step_period = 1;
    uint32_t period_counter = 0;
};

struct VoiceState
{
    bool playing = false;
    bool ended = true;
    bool key_on = false;

    uint32_t vag_addr = 0;
    uint32_t vag_size = 0;
    uint32_t data_start_offset = 0;
    uint32_t loop_start_offset = 0;
    uint32_t block_offset = 0;
    bool loop = false;
    bool at_end_block = false;

    int32_t hist1 = 0;
    int32_t hist2 = 0;
    std::array<int16_t, kVagSamplesPerBlock> decoded_block{};
    int16_t prev_sample = 0;
    uint32_t sample_pos_fp = static_cast<uint32_t>(kVagSamplesPerBlock) << 12;

    int32_t pitch = kSasPitchBase;
    int32_t left_vol = kSasVolumeMax;
    int32_t right_vol = kSasVolumeMax;
    int32_t effect_left_vol = 0;
    int32_t effect_right_vol = 0;

    AdsrPhase env_phase = AdsrPhase::kOff;
    int64_t env_level = 0;
    int64_t sustain_level = kEnvMax;
    EnvelopeRate attack{};
    EnvelopeRate decay{ AdsrCurve::kExpDec, 0, 1, 0 };
    EnvelopeRate sustain{ AdsrCurve::kLinearInc, 0, 1, 0 };
    EnvelopeRate release{ AdsrCurve::kExpDec, kEnvMax, 1, 0 };
};

struct SasCoreInstance
{
    uint32_t core_addr = 0;
    int32_t grain = 256;
    int32_t max_voices = kSasMaxVoices;
    int32_t out_mode = kSasOutputModeInterleaved;
    int32_t sample_rate = kSasDefaultSampleRate;

    int32_t reverb_type = -1;
    int32_t reverb_left_vol = 0;
    int32_t reverb_right_vol = 0;
    bool reverb_dry = true;
    bool reverb_wet = false;

    std::array<VoiceState, kSasMaxVoices> voices{};
};

struct SasGlobalState
{
    std::mutex mutex;
    bool registered = false;
    std::unordered_map<uint32_t, SasCoreInstance> instances;
    uint32_t primary_core = 0;
    uint64_t core_calls = 0;
    uint64_t frames_synthesized = 0;
    uint64_t key_ons = 0;
    uint64_t key_offs = 0;
    uint64_t voices_set = 0;
};

SasGlobalState g_sas;

int16_t ClampS16(int64_t v)
{
    if (v > 32767)
    {
        return 32767;
    }
    if (v < -32768)
    {
        return -32768;
    }
    return static_cast<int16_t>(v);
}

EnvelopeRate MakeSpuRate(bool exp, bool dec, uint32_t shift, uint32_t step)
{
    EnvelopeRate rate{};
    if (exp)
    {
        rate.curve = dec ? AdsrCurve::kExpDec : AdsrCurve::kExpInc;
    }
    else
    {
        rate.curve = dec ? AdsrCurve::kLinearDec : AdsrCurve::kLinearInc;
    }

    if (shift >= 31)
    {
        rate.step_per_sample = 0;
        rate.step_period = 1;
        return rate;
    }

    int64_t base_step = dec ? static_cast<int64_t>(-8 + static_cast<int32_t>(step))
                            : static_cast<int64_t>(7 - static_cast<int32_t>(step));
    if (shift < 11)
    {
        rate.step_per_sample = (base_step << (11 - shift)) * kSdlRateScale;
        rate.step_period = 1;
    }
    else
    {
        const uint32_t period_shift = std::min<uint32_t>(shift - 11, 20);
        rate.step_per_sample = base_step * kSdlRateScale;
        rate.step_period = 1u << period_shift;
    }
    return rate;
}

uint32_t ComputeEndFlags(const SasCoreInstance& inst)
{
    uint32_t flags = 0xFFFFFFFFu;
    for (int32_t v = 0; v < inst.max_voices && v < kSasMaxVoices; ++v)
    {
        if (!inst.voices[static_cast<size_t>(v)].ended)
        {
            flags &= ~(1u << v);
        }
    }
    return flags;
}

bool ValidateVoice(const SasCoreInstance& inst, int32_t voice)
{
    return voice >= 0 && voice < inst.max_voices && voice < kSasMaxVoices;
}

bool DecodeNextVagBlock(VoiceState& v, runtime::GuestMemory& mem)
{
    if (v.vag_addr == 0 || v.vag_size < kVagBlockBytes || v.at_end_block)
    {
        return false;
    }

    if (v.block_offset + kVagBlockBytes > v.vag_size)
    {
        if (v.loop && v.loop_start_offset + kVagBlockBytes <= v.vag_size)
        {
            v.block_offset = v.loop_start_offset;
        }
        else
        {
            return false;
        }
    }

    const uint32_t addr = v.vag_addr + v.block_offset;
    if (!mem.IsValidAddress(addr, kVagBlockBytes))
    {
        return false;
    }

    std::array<uint8_t, kVagBlockBytes> block{};
    for (size_t i = 0; i < kVagBlockBytes; ++i)
    {
        block[i] = mem.ReadU8(addr + static_cast<uint32_t>(i));
    }

    const uint8_t predict_shift = block[0];
    uint32_t shift = predict_shift & 0x0Fu;
    if (shift > 12)
    {
        shift = 9;
    }
    uint32_t filter = (predict_shift >> 4) & 0x0Fu;
    if (filter >= kVagFilterK0.size())
    {
        filter = 0;
    }

    const uint8_t flags = block[1];
    if ((flags & kVagFlagLoopStart) != 0)
    {
        v.loop_start_offset = v.block_offset;
    }

    const bool end_mute = (flags & kVagFlagEndMute) == kVagFlagEndMute;
    if (end_mute)
    {
        v.decoded_block.fill(0);
        v.at_end_block = true;
        return false;
    }

    const int32_t k0 = kVagFilterK0[filter];
    const int32_t k1 = kVagFilterK1[filter];

    for (size_t i = 0; i < 14; ++i)
    {
        const uint8_t packed = block[2 + i];
        for (int nib_idx = 0; nib_idx < 2; ++nib_idx)
        {
            const uint8_t nib = (nib_idx == 0) ? (packed & 0x0Fu) : ((packed >> 4) & 0x0Fu);
            int32_t s = static_cast<int16_t>(static_cast<uint16_t>(nib) << 12);
            s >>= shift;
            s += (v.hist1 * k0 + v.hist2 * k1) >> 6;
            const int16_t clamped = ClampS16(s);
            v.hist2 = v.hist1;
            v.hist1 = clamped;
            v.decoded_block[i * 2 + static_cast<size_t>(nib_idx)] = clamped;
        }
    }

    v.block_offset += static_cast<uint32_t>(kVagBlockBytes);

    if ((flags & kVagFlagLoopEnd) != 0)
    {
        if (((flags & kVagFlagLoopRepeat) != 0 || v.loop) &&
            v.loop_start_offset + kVagBlockBytes <= v.vag_size)
        {
            v.block_offset = v.loop_start_offset;
        }
        else
        {
            v.at_end_block = true;
        }
    }
    return true;
}

void StepEnvelopeRate(VoiceState& v, EnvelopeRate& rate)
{
    if (rate.step_per_sample == 0)
    {
        return;
    }
    rate.period_counter += 1;
    if (rate.period_counter < rate.step_period)
    {
        return;
    }
    rate.period_counter = 0;

    int64_t delta = rate.step_per_sample;
    if (rate.curve == AdsrCurve::kExpInc)
    {
        if (v.env_level > kEnvExpThreshold)
        {
            delta = std::max<int64_t>(1, delta / 4);
        }
    }
    else if (rate.curve == AdsrCurve::kExpDec)
    {
        // Proportional exponential decay in 30-bit space, keeping minimum step to reach zero cleanly.
        const int64_t scaled = (delta * v.env_level) / kEnvMax;
        delta = (delta < 0) ? std::min<int64_t>(-1, scaled) : scaled;
    }

    v.env_level = std::clamp<int64_t>(v.env_level + delta, 0, kEnvMax);
}

void AdvanceEnvelope(VoiceState& v)
{
    switch (v.env_phase)
    {
    case AdsrPhase::kOff:
        v.env_level = 0;
        break;
    case AdsrPhase::kAttack:
        StepEnvelopeRate(v, v.attack);
        if (v.env_level >= kEnvMax)
        {
            v.env_level = kEnvMax;
            v.env_phase = AdsrPhase::kDecay;
        }
        break;
    case AdsrPhase::kDecay:
        if (v.env_level <= v.sustain_level)
        {
            v.env_level = v.sustain_level;
            v.env_phase = AdsrPhase::kSustain;
            break;
        }
        StepEnvelopeRate(v, v.decay);
        if (v.env_level <= v.sustain_level)
        {
            v.env_level = v.sustain_level;
            v.env_phase = AdsrPhase::kSustain;
        }
        break;
    case AdsrPhase::kSustain:
        StepEnvelopeRate(v, v.sustain);
        break;
    case AdsrPhase::kRelease:
        StepEnvelopeRate(v, v.release);
        if (v.env_level <= 0)
        {
            v.env_level = 0;
            v.env_phase = AdsrPhase::kOff;
            v.playing = false;
            v.ended = true;
        }
        break;
    }
}

void MixVoiceSample(VoiceState& v, runtime::GuestMemory& mem, int64_t& out_left, int64_t& out_right)
{
    if (!v.playing || v.ended)
    {
        return;
    }

    while ((v.sample_pos_fp >> 12) >= static_cast<uint32_t>(kVagSamplesPerBlock))
    {
        v.prev_sample = v.decoded_block[static_cast<size_t>(kVagSamplesPerBlock - 1)];
        if (!DecodeNextVagBlock(v, mem))
        {
            v.playing = false;
            v.ended = true;
            v.env_phase = AdsrPhase::kOff;
            v.env_level = 0;
            return;
        }
        v.sample_pos_fp -= static_cast<uint32_t>(kVagSamplesPerBlock) << 12;
    }

    const uint32_t idx = v.sample_pos_fp >> 12;
    const int64_t frac = static_cast<int64_t>(v.sample_pos_fp & 0xFFFu);
    const int32_t s0 = v.decoded_block[idx];
    const int32_t s_prev = (idx == 0) ? v.prev_sample : v.decoded_block[idx - 1];
    // Linear interpolation between previous sample and current sample when fractional phase > 0.
    const int64_t sample = (frac == 0) ? s0 : (s_prev + (((s0 - s_prev) * frac) >> 12));

    AdvanceEnvelope(v);
    if (!v.playing)
    {
        return;
    }

    // Apply envelope (0..2^30 -> shift 30) and voice L/R volume (0..0x1000 -> shift 12).
    const int64_t env_sample = (sample * v.env_level) >> 30;
    out_left += (env_sample * v.left_vol) >> 12;
    out_right += (env_sample * v.right_vol) >> 12;

    v.sample_pos_fp += static_cast<uint32_t>(std::max<int32_t>(0, v.pitch));
}

void HleSasInit()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t grain = recomp::Arg<int32_t>(1);
    const int32_t max_voices = recomp::Arg<int32_t>(2);
    const int32_t out_mode = recomp::Arg<int32_t>(3);
    const int32_t sample_rate = recomp::Arg<int32_t>(4);

    auto& mem = runtime::GetGuestMemory();
    if (core == 0 || (core & 0x3Fu) != 0 || !mem.IsValidAddress(core, 4))
    {
        recomp::Return(kSceSasErrorInvalidParameter);
        return;
    }
    if (grain < kSasMinGrain || grain > kSasMaxGrain || (grain & 0x1F) != 0)
    {
        recomp::Return(kSceSasErrorInvalidGrain);
        return;
    }
    if (max_voices <= 0 || max_voices > kSasMaxVoices)
    {
        recomp::Return(kSceSasErrorInvalidMaxVoices);
        return;
    }
    if (out_mode != kSasOutputModeInterleaved && out_mode != kSasOutputModeRaw)
    {
        recomp::Return(kSceSasErrorInvalidParameter);
        return;
    }
    if (sample_rate <= 0 || (sample_rate != 44100 && sample_rate != 22050))
    {
        recomp::Return(kSceSasErrorInvalidParameter);
        return;
    }

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    SasCoreInstance inst{};
    inst.core_addr = core;
    inst.grain = grain;
    inst.max_voices = max_voices;
    inst.out_mode = out_mode;
    inst.sample_rate = sample_rate;
    g_sas.instances[core] = inst;
    if (g_sas.primary_core == 0)
    {
        g_sas.primary_core = core;
    }

    recomp::Return(int32_t{ 0 });
}

void HleSasCore()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const uint32_t sas_out = recomp::Arg<uint32_t>(1);

    auto& mem = runtime::GetGuestMemory();
    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }

    SasCoreInstance& inst = it->second;
    const size_t out_bytes = static_cast<size_t>(inst.grain) * 2 * sizeof(int16_t);
    if (sas_out == 0 || !mem.IsValidAddress(sas_out, out_bytes))
    {
        recomp::Return(kSceSasErrorInvalidParameter);
        return;
    }

    uint8_t* dst_bytes = mem.Translate(sas_out);
    if (dst_bytes == nullptr)
    {
        recomp::Return(kSceSasErrorInvalidParameter);
        return;
    }

    const int32_t grain = inst.grain;
    for (int32_t f = 0; f < grain; ++f)
    {
        int64_t left = 0;
        int64_t right = 0;
        for (int32_t v = 0; v < inst.max_voices; ++v)
        {
            MixVoiceSample(inst.voices[static_cast<size_t>(v)], mem, left, right);
        }

        const int16_t s_left = ClampS16(left);
        const int16_t s_right = ClampS16(right);
        if (inst.out_mode == kSasOutputModeRaw)
        {
            mem.WriteU16(sas_out + static_cast<uint32_t>(f * 2), static_cast<uint16_t>(s_left));
            mem.WriteU16(sas_out + static_cast<uint32_t>((grain + f) * 2),
                         static_cast<uint16_t>(s_right));
        }
        else
        {
            mem.WriteU16(sas_out + static_cast<uint32_t>(f * 4), static_cast<uint16_t>(s_left));
            mem.WriteU16(sas_out + static_cast<uint32_t>(f * 4 + 2),
                         static_cast<uint16_t>(s_right));
        }
    }

    g_sas.core_calls += 1;
    g_sas.frames_synthesized += static_cast<uint64_t>(grain);
    recomp::Return(int32_t{ 0 });
}

void HleSasSetVoice()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t voice = recomp::Arg<int32_t>(1);
    const uint32_t vag_addr = recomp::Arg<uint32_t>(2);
    const int32_t size = recomp::Arg<int32_t>(3);
    const int32_t loop = recomp::Arg<int32_t>(4);

    auto& mem = runtime::GetGuestMemory();
    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }
    if (!ValidateVoice(it->second, voice))
    {
        recomp::Return(kSceSasErrorInvalidVoice);
        return;
    }
    if (vag_addr == 0 || size < static_cast<int32_t>(kVagBlockBytes) ||
        !mem.IsValidAddress(vag_addr, static_cast<size_t>(size)))
    {
        recomp::Return(kSceSasErrorInvalidParameter);
        return;
    }

    uint32_t start_off = 0;
    if (static_cast<size_t>(size) >= kVagHeaderSize && mem.ReadU32(vag_addr) == kVagHeaderMagic)
    {
        start_off = static_cast<uint32_t>(kVagHeaderSize);
    }

    VoiceState& v = it->second.voices[static_cast<size_t>(voice)];
    v.vag_addr = vag_addr;
    v.vag_size = static_cast<uint32_t>(size);
    v.data_start_offset = start_off;
    v.loop_start_offset = start_off;
    v.block_offset = start_off;
    v.loop = (loop != 0);
    v.at_end_block = false;
    v.hist1 = 0;
    v.hist2 = 0;
    v.decoded_block.fill(0);
    v.prev_sample = 0;
    v.sample_pos_fp = static_cast<uint32_t>(kVagSamplesPerBlock) << 12;

    g_sas.voices_set += 1;
    recomp::Return(int32_t{ 0 });
}

void HleSasSetKeyOn()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t voice = recomp::Arg<int32_t>(1);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }
    if (!ValidateVoice(it->second, voice))
    {
        recomp::Return(kSceSasErrorInvalidVoice);
        return;
    }

    VoiceState& v = it->second.voices[static_cast<size_t>(voice)];
    v.key_on = true;
    v.block_offset = v.data_start_offset;
    v.loop_start_offset = v.data_start_offset;
    v.at_end_block = false;
    v.hist1 = 0;
    v.hist2 = 0;
    v.prev_sample = 0;
    v.sample_pos_fp = static_cast<uint32_t>(kVagSamplesPerBlock) << 12;

    v.env_phase = AdsrPhase::kAttack;
    v.env_level = 0;
    v.attack.period_counter = 0;
    v.decay.period_counter = 0;
    v.sustain.period_counter = 0;
    v.release.period_counter = 0;

    if (v.vag_addr != 0 && v.vag_size >= v.data_start_offset + kVagBlockBytes)
    {
        v.playing = true;
        v.ended = false;
    }
    else
    {
        v.playing = false;
        v.ended = true;
    }

    g_sas.key_ons += 1;
    recomp::Return(int32_t{ 0 });
}

void HleSasSetKeyOff()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t voice = recomp::Arg<int32_t>(1);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }
    if (!ValidateVoice(it->second, voice))
    {
        recomp::Return(kSceSasErrorInvalidVoice);
        return;
    }

    VoiceState& v = it->second.voices[static_cast<size_t>(voice)];
    v.key_on = false;
    if (v.playing)
    {
        v.env_phase = AdsrPhase::kRelease;
        v.release.period_counter = 0;
    }
    else
    {
        v.env_phase = AdsrPhase::kOff;
        v.env_level = 0;
        v.ended = true;
    }

    g_sas.key_offs += 1;
    recomp::Return(int32_t{ 0 });
}

void HleSasSetPitch()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t voice = recomp::Arg<int32_t>(1);
    const int32_t pitch = recomp::Arg<int32_t>(2);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }
    if (!ValidateVoice(it->second, voice))
    {
        recomp::Return(kSceSasErrorInvalidVoice);
        return;
    }
    if (pitch < 0 || pitch > kSasPitchMax)
    {
        recomp::Return(kSceSasErrorInvalidParameter);
        return;
    }

    it->second.voices[static_cast<size_t>(voice)].pitch = pitch;
    recomp::Return(int32_t{ 0 });
}

void HleSasSetVolume()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t voice = recomp::Arg<int32_t>(1);
    const int32_t left_vol = recomp::Arg<int32_t>(2);
    const int32_t right_vol = recomp::Arg<int32_t>(3);
    const int32_t effect_left = recomp::Arg<int32_t>(4);
    const int32_t effect_right = recomp::Arg<int32_t>(5);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }
    if (!ValidateVoice(it->second, voice))
    {
        recomp::Return(kSceSasErrorInvalidVoice);
        return;
    }

    auto valid_vol = [](int32_t v) { return v >= -kSasVolumeMax && v <= kSasVolumeMax; };
    if (!valid_vol(left_vol) || !valid_vol(right_vol) || !valid_vol(effect_left) ||
        !valid_vol(effect_right))
    {
        recomp::Return(kSceSasErrorInvalidParameter);
        return;
    }

    VoiceState& v = it->second.voices[static_cast<size_t>(voice)];
    v.left_vol = left_vol;
    v.right_vol = right_vol;
    v.effect_left_vol = effect_left;
    v.effect_right_vol = effect_right;
    recomp::Return(int32_t{ 0 });
}

void HleSasSetAdsr()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t voice = recomp::Arg<int32_t>(1);
    const int32_t flag = recomp::Arg<int32_t>(2);
    const int32_t attack_rate = recomp::Arg<int32_t>(3);
    const int32_t decay_rate = recomp::Arg<int32_t>(4);
    const int32_t sustain_rate = recomp::Arg<int32_t>(5);
    const int32_t release_rate = recomp::Arg<int32_t>(6);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }
    if (!ValidateVoice(it->second, voice))
    {
        recomp::Return(kSceSasErrorInvalidVoice);
        return;
    }

    VoiceState& v = it->second.voices[static_cast<size_t>(voice)];
    if ((flag & 0x01) != 0)
    {
        v.attack.curve = AdsrCurve::kLinearInc;
        v.attack.step_per_sample = std::max<int64_t>(0, attack_rate);
        v.attack.step_period = 1;
    }
    if ((flag & 0x02) != 0)
    {
        v.decay.curve = AdsrCurve::kExpDec;
        v.decay.step_per_sample = -std::max<int64_t>(0, decay_rate);
        v.decay.step_period = 1;
    }
    if ((flag & 0x04) != 0)
    {
        v.sustain.curve = AdsrCurve::kLinearDec;
        v.sustain.step_per_sample = -std::max<int64_t>(0, sustain_rate);
        v.sustain.step_period = 1;
    }
    if ((flag & 0x08) != 0)
    {
        v.release.curve = AdsrCurve::kExpDec;
        v.release.step_per_sample = -std::max<int64_t>(0, release_rate);
        v.release.step_period = 1;
    }

    recomp::Return(int32_t{ 0 });
}

void HleSasSetSimpleAdsr()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t voice = recomp::Arg<int32_t>(1);
    const uint32_t env1 = recomp::Arg<uint32_t>(2);
    const uint32_t env2 = recomp::Arg<uint32_t>(3);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }
    if (!ValidateVoice(it->second, voice))
    {
        recomp::Return(kSceSasErrorInvalidVoice);
        return;
    }

    VoiceState& v = it->second.voices[static_cast<size_t>(voice)];
    const uint32_t sustain_lvl_nibble = env1 & 0x0Fu;
    v.sustain_level = ((static_cast<int64_t>(sustain_lvl_nibble) + 1) * kEnvMax) / 16;

    const uint32_t decay_shift = (env1 >> 4) & 0x0Fu;
    const uint32_t attack_step = (env1 >> 8) & 0x03u;
    const uint32_t attack_shift = (env1 >> 10) & 0x1Fu;
    const bool attack_exp = ((env1 >> 15) & 0x01u) != 0;

    const uint32_t release_shift = env2 & 0x1Fu;
    const bool release_exp = ((env2 >> 5) & 0x01u) != 0;
    const uint32_t sustain_step = (env2 >> 6) & 0x03u;
    const uint32_t sustain_shift = (env2 >> 8) & 0x1Fu;
    const bool sustain_dec = ((env2 >> 14) & 0x01u) != 0;
    const bool sustain_exp = ((env2 >> 15) & 0x01u) != 0;

    v.attack = MakeSpuRate(attack_exp, false, attack_shift, attack_step);
    v.decay = MakeSpuRate(true, true, decay_shift, 0);
    v.sustain = MakeSpuRate(sustain_exp, sustain_dec, sustain_shift, sustain_step);
    v.release = MakeSpuRate(release_exp, true, release_shift, 0);

    recomp::Return(int32_t{ 0 });
}

void HleSasGetEndFlag()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(uint32_t{ 0xFFFFFFFFu });
        return;
    }

    recomp::Return(ComputeEndFlags(it->second));
}

void HleSasRevType()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t type = recomp::Arg<int32_t>(1);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }
    // PSPSDK defines PSP_SAS_EFFECT_TYPE_OFF = -1; 0..9 are the hardware effect presets.
    if (type < -1 || type > 9)
    {
        recomp::Return(kSceSasErrorInvalidFxType);
        return;
    }

    it->second.reverb_type = type;
    recomp::Return(int32_t{ 0 });
}

void HleSasRevEvol()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t left_vol = recomp::Arg<int32_t>(1);
    const int32_t right_vol = recomp::Arg<int32_t>(2);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }

    it->second.reverb_left_vol = left_vol;
    it->second.reverb_right_vol = right_vol;
    recomp::Return(int32_t{ 0 });
}

void HleSasRevVon()
{
    const uint32_t core = recomp::Arg<uint32_t>(0);
    const int32_t dry = recomp::Arg<int32_t>(1);
    const int32_t wet = recomp::Arg<int32_t>(2);

    std::lock_guard<std::mutex> lock(g_sas.mutex);
    auto it = g_sas.instances.find(core);
    if (it == g_sas.instances.end())
    {
        recomp::Return(kSasErrorNotInitialized);
        return;
    }

    it->second.reverb_dry = (dry != 0);
    it->second.reverb_wet = (wet != 0);
    recomp::Return(int32_t{ 0 });
}

} // namespace

void Register()
{
    std::lock_guard<std::mutex> lock(g_sas.mutex);
    g_sas.instances.clear();
    g_sas.primary_core = 0;
    g_sas.core_calls = 0;
    g_sas.frames_synthesized = 0;
    g_sas.key_ons = 0;
    g_sas.key_offs = 0;
    g_sas.voices_set = 0;
    g_sas.registered = true;

    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceSasCore", "__sceSasInit", HleSasInit);
    reg.RegisterByName("sceSasCore", "__sceSasCore", HleSasCore);
    reg.RegisterByName("sceSasCore", "__sceSasSetVoice", HleSasSetVoice);
    reg.RegisterByName("sceSasCore", "__sceSasSetKeyOn", HleSasSetKeyOn);
    reg.RegisterByName("sceSasCore", "__sceSasSetKeyOff", HleSasSetKeyOff);
    reg.RegisterByName("sceSasCore", "__sceSasSetPitch", HleSasSetPitch);
    reg.RegisterByName("sceSasCore", "__sceSasSetVolume", HleSasSetVolume);
    reg.RegisterByName("sceSasCore", "__sceSasSetADSR", HleSasSetAdsr);
    reg.RegisterByName("sceSasCore", "__sceSasSetSimpleADSR", HleSasSetSimpleAdsr);
    reg.RegisterByName("sceSasCore", "__sceSasGetEndFlag", HleSasGetEndFlag);
    reg.RegisterByName("sceSasCore", "__sceSasRevType", HleSasRevType);
    reg.RegisterByName("sceSasCore", "__sceSasRevEVOL", HleSasRevEvol);
    reg.RegisterByName("sceSasCore", "__sceSasRevVON", HleSasRevVon);
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_sas.mutex);
    g_sas.instances.clear();
    g_sas.primary_core = 0;
    g_sas.registered = false;
}

SasStats GetStats()
{
    std::lock_guard<std::mutex> lock(g_sas.mutex);
    SasStats s{};
    s.instances = static_cast<uint32_t>(g_sas.instances.size());
    s.core_calls = g_sas.core_calls;
    s.frames_synthesized = g_sas.frames_synthesized;
    s.key_ons = g_sas.key_ons;
    s.key_offs = g_sas.key_offs;
    s.voices_set = g_sas.voices_set;
    s.reverb_applied = false;

    uint32_t active = 0;
    for (const auto& [addr, inst] : g_sas.instances)
    {
        for (int32_t v = 0; v < inst.max_voices && v < kSasMaxVoices; ++v)
        {
            if (inst.voices[static_cast<size_t>(v)].playing)
            {
                active += 1;
            }
        }
        if (addr == g_sas.primary_core)
        {
            s.end_flags = ComputeEndFlags(inst);
            s.reverb_type = inst.reverb_type;
            s.reverb_dry = inst.reverb_dry;
            s.reverb_wet = inst.reverb_wet;
        }
    }
    s.active_voices = active;
    return s;
}

std::string FormatStats()
{
    const SasStats s = GetStats();
    char buf[256];
    std::snprintf(buf,
                  sizeof(buf),
                  "SAS: instances=%u calls=%llu frames=%llu keyOn=%llu keyOff=%llu "
                  "activeVoices=%u revType=%d reverb=dry only (not mixed)",
                  s.instances,
                  static_cast<unsigned long long>(s.core_calls),
                  static_cast<unsigned long long>(s.frames_synthesized),
                  static_cast<unsigned long long>(s.key_ons),
                  static_cast<unsigned long long>(s.key_offs),
                  s.active_voices,
                  s.reverb_type);
    return std::string(buf);
}

} // namespace repsp::hle::sascore
