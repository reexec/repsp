#include "repsp/hle/kernel.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/hle/sascore.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

void MockSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void MockRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t MockGetGpr(int idx) { return (idx > 0 && idx < 32) ? g_cpu.gpr[idx] : 0; }
void MockSetGpr(int idx, uint32_t val) { if (idx > 0 && idx < 32) g_cpu.gpr[idx] = val; }
uint32_t MockGetFpr(int) { return 0; }
void MockSetFpr(int, uint32_t) {}
void MockCallGuest(uint32_t) {}

int32_t CallNid7(uint32_t nid,
                 uint32_t a0 = 0,
                 uint32_t a1 = 0,
                 uint32_t a2 = 0,
                 uint32_t a3 = 0,
                 uint32_t t0 = 0,
                 uint32_t t1 = 0,
                 uint32_t t2 = 0)
{
    MockSetGpr(4, a0);
    MockSetGpr(5, a1);
    MockSetGpr(6, a2);
    MockSetGpr(7, a3);
    MockSetGpr(8, t0);
    MockSetGpr(9, t1);
    MockSetGpr(10, t2);
    repsp_recomp_hle_call(nid);
    return static_cast<int32_t>(MockGetGpr(2));
}

constexpr uint32_t kNidSasInit = 0x42778A9Fu;
constexpr uint32_t kNidSasCore = 0xA3589D81u;
constexpr uint32_t kNidSasSetVoice = 0x99944089u;
constexpr uint32_t kNidSasSetKeyOn = 0x76F01ACAu;
constexpr uint32_t kNidSasSetKeyOff = 0xA0CF2FA4u;
constexpr uint32_t kNidSasSetPitch = 0xAD84D37Fu;
constexpr uint32_t kNidSasSetVolume = 0x440CA7D8u;
constexpr uint32_t kNidSasSetAdsr = 0x019B25EBu;
constexpr uint32_t kNidSasSetSimpleAdsr = 0xCBCD4F79u;
constexpr uint32_t kNidSasGetEndFlag = 0x68A46B95u;
constexpr uint32_t kNidSasRevType = 0x33D4AB37u;
constexpr uint32_t kNidSasRevEvol = 0xD5A229C9u;
constexpr uint32_t kNidSasRevVon = 0xF983B186u;

constexpr uint32_t kCoreAddr = 0x08950000u; // 64-byte aligned
constexpr uint32_t kOutAddr = 0x08952000u;
constexpr uint32_t kVagRawAddr = 0x08954000u;
constexpr uint32_t kVagHdrAddr = 0x08955000u;

void WriteVagBlock(repsp::runtime::GuestMemory& mem,
                   uint32_t addr,
                   uint8_t filter_shift,
                   uint8_t flags,
                   uint8_t nibble_byte)
{
    mem.WriteU8(addr + 0, filter_shift);
    mem.WriteU8(addr + 1, flags);
    for (uint32_t i = 2; i < 16; ++i)
    {
        mem.WriteU8(addr + i, nibble_byte);
    }
}

} // namespace

int main()
{
    using namespace repsp::hle;
    using namespace repsp::hle::sascore;

    auto& mem = repsp::runtime::GetGuestMemory();
    TEST_ASSERT(mem.Initialize());

    const repsp_guest_cpu_ops ops{
        sizeof(MockCpu), MockSave, MockRestore,
        MockGetGpr, MockSetGpr, MockCallGuest,
        MockGetFpr, MockSetFpr,
    };
    repsp_recomp_register_cpu(&ops);
    kernel::Register(kernel::KernelConfig{});
    Register();

    // 1. __sceSasInit parameter validation & initial end flag.
    TEST_ASSERT(CallNid7(kNidSasInit, 0u, 256u, 32u, 0u, 44100u) == kSceSasErrorInvalidParameter);
    TEST_ASSERT(CallNid7(kNidSasInit, kCoreAddr, 33u, 32u, 0u, 44100u) == kSceSasErrorInvalidGrain);
    TEST_ASSERT(CallNid7(kNidSasInit, kCoreAddr, 4096u, 32u, 0u, 44100u) == kSceSasErrorInvalidGrain);
    TEST_ASSERT(CallNid7(kNidSasInit, kCoreAddr, 64u, 33u, 0u, 44100u) == kSceSasErrorInvalidMaxVoices);
    TEST_ASSERT(CallNid7(kNidSasInit, kCoreAddr, 64u, 32u, 0u, 44100u) == 0);
    TEST_ASSERT(static_cast<uint32_t>(CallNid7(kNidSasGetEndFlag, kCoreAddr)) == 0xFFFFFFFFu);

    // 2. Reverb control parameter checks (dry-only mixer semantics; -1 = OFF).
    TEST_ASSERT(CallNid7(kNidSasRevType, kCoreAddr, static_cast<uint32_t>(-2)) == kSceSasErrorInvalidFxType);
    TEST_ASSERT(CallNid7(kNidSasRevType, kCoreAddr, 99u) == kSceSasErrorInvalidFxType);
    TEST_ASSERT(CallNid7(kNidSasRevType, kCoreAddr, static_cast<uint32_t>(-1)) == 0);
    TEST_ASSERT(CallNid7(kNidSasRevType, kCoreAddr, 5u) == 0);
    TEST_ASSERT(CallNid7(kNidSasRevEvol, kCoreAddr, 0x0800u, 0x0800u) == 0);
    TEST_ASSERT(CallNid7(kNidSasRevVon, kCoreAddr, 1u, 1u) == 0);

    // 3. One-shot raw VAG decode (Filter 0, Shift 0, nibble 1 -> +4096) & EndFlag transition.
    WriteVagBlock(mem, kVagRawAddr, 0x00, 0x01 /*end, no loop*/, 0x11);
    TEST_ASSERT(CallNid7(kNidSasSetVoice, kCoreAddr, 32u, kVagRawAddr, 16u, 0u) ==
                kSceSasErrorInvalidVoice);
    TEST_ASSERT(CallNid7(kNidSasSetVoice, kCoreAddr, 0u, kVagRawAddr, 16u, 0u) == 0);
    TEST_ASSERT(CallNid7(kNidSasSetPitch, kCoreAddr, 0u, static_cast<uint32_t>(kSasPitchBase)) == 0);
    TEST_ASSERT(CallNid7(kNidSasSetVolume, kCoreAddr, 0u, 0x1000u, 0x0800u, 0u, 0u) == 0);
    TEST_ASSERT(CallNid7(kNidSasSetSimpleAdsr, kCoreAddr, 0u, 0x000Fu, 0x5FC0u) == 0);
    TEST_ASSERT(CallNid7(kNidSasSetKeyOn, kCoreAddr, 0u) == 0);
    TEST_ASSERT((static_cast<uint32_t>(CallNid7(kNidSasGetEndFlag, kCoreAddr)) & 1u) == 0u);

    // Grain 64 consumes all 28 decoded VAG samples then hits end block and silences remaining samples.
    TEST_ASSERT(CallNid7(kNidSasCore, kCoreAddr, kOutAddr) == 0);
    TEST_ASSERT((static_cast<uint32_t>(CallNid7(kNidSasGetEndFlag, kCoreAddr)) & 1u) == 1u);

    // Instant attack (step=7, shift=0) reaches full scale in ~3 samples -> sample 10 must be steady.
    const int16_t s10_l = static_cast<int16_t>(mem.ReadU16(kOutAddr + 10u * 4u));
    const int16_t s10_r = static_cast<int16_t>(mem.ReadU16(kOutAddr + 10u * 4u + 2u));
    const int16_t s40_l = static_cast<int16_t>(mem.ReadU16(kOutAddr + 40u * 4u));
    TEST_ASSERT(s10_l > 4000 && s10_l <= 4096);
    TEST_ASSERT(s10_r == s10_l / 2);
    TEST_ASSERT(s40_l == 0);

    // 4. VAGp 48-byte header + Filter 1 + loop=1 + custom SetADSR + 2x pitch + KeyOff release.
    mem.WriteU32(kVagHdrAddr + 0, 0x70474156u); // "VAGp" LE
    for (uint32_t i = 4; i < 48; ++i)
    {
        mem.WriteU8(kVagHdrAddr + i, 0);
    }
    WriteVagBlock(mem, kVagHdrAddr + 48u, 0x14 /*Filter 1, shift 4*/, 0x04 /*loop start*/, 0x11);
    WriteVagBlock(mem, kVagHdrAddr + 64u, 0x14, 0x03 /*loop end+repeat*/, 0x11);

    TEST_ASSERT(CallNid7(kNidSasSetVoice, kCoreAddr, 1u, kVagHdrAddr, 80u, 1u) == 0);
    TEST_ASSERT(CallNid7(kNidSasSetPitch, kCoreAddr, 1u, 0x2000u) == 0);
    TEST_ASSERT(CallNid7(kNidSasSetVolume, kCoreAddr, 1u, 0x1000u, 0x1000u, 0u, 0u) == 0);
    TEST_ASSERT(CallNid7(kNidSasSetAdsr, kCoreAddr, 1u, 0x0Fu, 0x40000000u, 0u, 0u, 0x10000000u) == 0);
    TEST_ASSERT(CallNid7(kNidSasSetKeyOn, kCoreAddr, 1u) == 0);

    for (int g = 0; g < 3; ++g)
    {
        TEST_ASSERT(CallNid7(kNidSasCore, kCoreAddr, kOutAddr) == 0);
    }
    // Loop voice must still be playing (bit 1 cleared).
    TEST_ASSERT((static_cast<uint32_t>(CallNid7(kNidSasGetEndFlag, kCoreAddr)) & (1u << 1)) == 0u);
    const int16_t loop_s5_l = static_cast<int16_t>(mem.ReadU16(kOutAddr + 5u * 4u));
    TEST_ASSERT(loop_s5_l > 0);

    TEST_ASSERT(CallNid7(kNidSasSetKeyOff, kCoreAddr, 1u) == 0);
    for (int g = 0; g < 5; ++g)
    {
        TEST_ASSERT(CallNid7(kNidSasCore, kCoreAddr, kOutAddr) == 0);
    }
    TEST_ASSERT((static_cast<uint32_t>(CallNid7(kNidSasGetEndFlag, kCoreAddr)) & (1u << 1)) != 0u);

    const SasStats stats = GetStats();
    TEST_ASSERT(stats.instances == 1);
    TEST_ASSERT(stats.core_calls == 9);
    TEST_ASSERT(stats.frames_synthesized == 9 * 64);
    TEST_ASSERT(stats.reverb_type == 5);
    TEST_ASSERT(stats.reverb_dry && stats.reverb_wet);
    TEST_ASSERT(!stats.reverb_applied);
    TEST_ASSERT(FormatStats().find("reverb=dry only (not mixed)") != std::string::npos);

    Shutdown();
    mem.Shutdown();

    std::printf("[PASS] sas_probe: 4/4 checks passed\n");
    return 0;
}
