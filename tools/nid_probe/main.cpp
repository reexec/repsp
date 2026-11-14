#include "repsp/runtime/nid_names.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/sha1.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define TEST_ASSERT(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::exit(1); \
        } \
    } while (0)

namespace
{

bool VerifySha1Hex(const void* data, size_t length, const char* expected_hex)
{
    uint8_t digest[20];
    repsp::runtime::ComputeSha1(static_cast<const uint8_t*>(data), length, digest);

    char hex_str[41];
    for (int i = 0; i < 20; ++i)
    {
        std::snprintf(hex_str + i * 2, 3, "%02x", digest[i]);
    }
    hex_str[40] = '\0';

    return std::strcmp(hex_str, expected_hex) == 0;
}

} // namespace

int main()
{
    using namespace repsp::runtime;

    // 1. FIPS 180 SHA-1 standard test vectors
    // Vector 1: Empty string ""
    TEST_ASSERT(VerifySha1Hex("", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"));

    // Vector 2: "abc"
    const char vec2[] = "abc";
    TEST_ASSERT(VerifySha1Hex(vec2, 3, "a9993e364706816aba3e25717850c26c9cd0d89d"));

    // Vector 3: 448-bit message (56 bytes) "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    const char vec3[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    TEST_ASSERT(VerifySha1Hex(vec3, 56, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"));

    // Vector 4: 1,000,000 repetitions of 'a'
    std::string vec4(1000000, 'a');
    TEST_ASSERT(VerifySha1Hex(vec4.data(), vec4.size(), "34aa973cd4c4daa4f61eeb2bdbad27316534016f"));

    // 2. Published PSP firmware NIDs (Sources: psdevwiki, uOFW)
    // - sceDisplaySetMode: 0x0E20F177 (source: https://www.psdevwiki.com/psp/SceDisplay)
    TEST_ASSERT(ComputeNid("sceDisplaySetMode") == 0x0E20F177);

    // - sceCtrlReadBufferPositive: 0x1F803938 (source: https://www.psdevwiki.com/psp/SceCtrl)
    TEST_ASSERT(ComputeNid("sceCtrlReadBufferPositive") == 0x1F803938);

    // - sceCtrlPeekBufferPositive: 0x3A622550 (source: https://www.psdevwiki.com/psp/SceCtrl)
    TEST_ASSERT(ComputeNid("sceCtrlPeekBufferPositive") == 0x3A622550);

    // - sceKernelExitGame: 0x05572A5F (source: https://www.psdevwiki.com/psp/SceLoadExec)
    TEST_ASSERT(ComputeNid("sceKernelExitGame") == 0x05572A5F);

    // 3. Test NID registry registration, dispatch, and unimplemented statistics
    NidRegistry registry;
    int call_count = 0;
    registry.RegisterByName("TestModule", "test_handler", [&call_count]()
    {
        ++call_count;
    });

    uint32_t test_nid = ComputeNid("test_handler");
    const HleEntry* entry = registry.Find(test_nid);
    TEST_ASSERT(entry != nullptr);
    TEST_ASSERT(entry->function_name == "test_handler");
    TEST_ASSERT(entry->module_name == "TestModule");

    bool invoked = registry.Invoke(test_nid);
    TEST_ASSERT(invoked);
    TEST_ASSERT(call_count == 1);

    // 4. Test unimplemented NID invocation
    uint32_t unreg_nid = 0xDEADBEEF;
    TEST_ASSERT(registry.Find(unreg_nid) == nullptr);

    TEST_ASSERT(!registry.Invoke(unreg_nid));
    TEST_ASSERT(registry.GetUnimplementedCount(unreg_nid) == 1);

    TEST_ASSERT(!registry.Invoke(unreg_nid));
    TEST_ASSERT(registry.GetUnimplementedCount(unreg_nid) == 2);

    const auto& counts = registry.GetUnimplementedCounts();
    TEST_ASSERT(counts.find(unreg_nid) != counts.end());
    TEST_ASSERT(counts.at(unreg_nid) == 2);

    // 5. Verify all 65 kernel/UMD NIDs from the initial consumer import table
    struct NidExpected { const char* name; uint32_t nid; };
    static const NidExpected kKernelNids[] = {
        { "sceKernelCreateThread", 0x446D8DE6u },
        { "sceKernelStartThread", 0xF475845Du },
        { "sceKernelExitThread", 0xAA73C935u },
        { "sceKernelDeleteThread", 0x9FA03CD3u },
        { "sceKernelTerminateDeleteThread", 0x383F7BCCu },
        { "sceKernelWaitThreadEnd", 0x278C0DF5u },
        { "sceKernelGetThreadId", 0x293B45B8u },
        { "sceKernelChangeThreadPriority", 0x71BC9871u },
        { "sceKernelSleepThread", 0x9ACE131Eu },
        { "sceKernelWakeupThread", 0xD59EAD2Fu },
        { "sceKernelDelayThread", 0xCEADEB47u },
        { "sceKernelDelayThreadCB", 0x68DA9E36u },
        { "sceKernelCreateSema", 0xD6DA4BA1u },
        { "sceKernelDeleteSema", 0x28B6489Cu },
        { "sceKernelWaitSema", 0x4E3A1105u },
        { "sceKernelSignalSema", 0x3F53E640u },
        { "sceKernelCreateEventFlag", 0x55C20A00u },
        { "sceKernelDeleteEventFlag", 0xEF9E4C70u },
        { "sceKernelSetEventFlag", 0x1FB15A32u },
        { "sceKernelClearEventFlag", 0x812346E4u },
        { "sceKernelWaitEventFlag", 0x402FCF22u },
        { "sceKernelPollEventFlag", 0x30FD48F0u },
        { "sceKernelReferEventFlagStatus", 0xA66B0120u },
        { "sceKernelCreateCallback", 0xE81CAF8Fu },
        { "sceKernelCheckCallback", 0x349D6D6Cu },
        { "sceKernelGetSystemTimeWide", 0x82BC5777u },
        { "sceKernelGetSystemTimeLow", 0x369ED59Du },
        { "sceIoOpen", 0x109F50BCu },
        { "sceIoClose", 0x810C4BC3u },
        { "sceIoRead", 0x6A638D83u },
        { "sceIoWrite", 0x42EC03ACu },
        { "sceIoLseek", 0x27EB27B8u },
        { "sceIoOpenAsync", 0x89AA9906u },
        { "sceIoReadAsync", 0xA0B5A7C2u },
        { "sceIoLseekAsync", 0x71B19E77u },
        { "sceIoWaitAsync", 0xE23EEC33u },
        { "sceIoWaitAsyncCB", 0x35DBD746u },
        { "sceIoPollAsync", 0x3251EA56u },
        { "sceKernelLoadModule", 0x977DE386u },
        { "sceKernelStartModule", 0x50F0C1ECu },
        { "sceKernelStopModule", 0xD1FF982Au },
        { "sceKernelUnloadModule", 0x2E0911AAu },
        { "sceKernelSelfStopUnloadModule", 0xD675EBB8u },
        { "sceKernelAllocPartitionMemory", 0x237DBD4Fu },
        { "sceKernelFreePartitionMemory", 0xB6D61D02u },
        { "sceKernelGetBlockHeadAddr", 0x9D9A5BA1u },
        { "sceKernelDcacheWritebackAll", 0x79D1C3FAu },
        { "sceKernelDcacheWritebackInvalidateAll", 0xB435DEC5u },
        { "sceKernelGetGPI", 0x37FB5C42u },
        { "sceKernelSetGPO", 0x6AD345D7u },
        { "sceKernelStdin", 0x172D316Eu },
        { "sceKernelStdout", 0xA6BAB2E9u },
        { "sceKernelStderr", 0xF78BA90Au },
        { "sceKernelCpuSuspendIntr", 0x092968F4u },
        { "sceKernelCpuResumeIntr", 0x5F10D406u },
        { "sceKernelExitGame", 0x05572A5Fu },
        { "sceKernelRegisterExitCallback", 0x4AC57943u },
        { "sceKernelPowerTick", 0x090CCB3Fu },
        { "scePowerRegisterCallback", 0x04B7766Eu },
        { "sceUmdActivate", 0xC6183D47u },
        { "sceUmdDeactivate", 0xE83742BAu },
        { "sceUmdGetDriveStat", 0x6B4A146Cu },
        { "sceUmdWaitDriveStat", 0x8EF08FCEu },
        { "sceUmdWaitDriveStatWithTimer", 0x56202973u },
        { "sceUmdCancelWaitDriveStat", 0x6AF9B50Au },
    };
    static_assert(sizeof(kKernelNids) / sizeof(kKernelNids[0]) == 65, "Expected 65 kernel NIDs");
    for (const auto& item : kKernelNids)
    {
        TEST_ASSERT(ComputeNid(item.name) == item.nid);
    }

    static const NidExpected kGeAndDisplayNids[] = {
        { "sceGeEdramGetAddr", 0xE47E40E4u },
        { "sceGeSetCallback", 0xA4FC06A4u },
        { "sceGeUnsetCallback", 0x05DB22CEu },
        { "sceGeListEnQueue", 0xAB49E76Au },
        { "sceGeListUpdateStallAddr", 0xE0D68148u },
        { "sceGeListSync", 0x03444EB4u },
        { "sceGeDrawSync", 0xB287BD61u },
        { "sceDisplaySetMode", 0x0E20F177u },
        { "sceDisplaySetFrameBuf", 0x289D82FEu },
        { "sceDisplayWaitVblankStart", 0x984C27E7u },
        { "sceDisplayWaitVblankStartCB", 0x46F186C3u },
        { "sceDisplayGetVcount", 0x9C6EAAD7u },
        { "sceDisplayGetFramePerSec", 0xDBA6C4C4u },
    };
    static_assert(sizeof(kGeAndDisplayNids) / sizeof(kGeAndDisplayNids[0]) == 13,
                  "Expected 13 GE+Display NIDs");
    for (const auto& item : kGeAndDisplayNids)
    {
        TEST_ASSERT(ComputeNid(item.name) == item.nid);
        TEST_ASSERT(repsp::runtime::LookupKnownNid(item.nid) != nullptr);
    }

    static const NidExpected kAudioNids[] = {
        // sceAudio (6)
        { "sceAudioChReserve", 0x5EC81C55u },
        { "sceAudioChRelease", 0x6FC46853u },
        { "sceAudioOutputBlocking", 0x136CAF51u },
        { "sceAudioOutputPannedBlocking", 0x13F592BCu },
        { "sceAudioGetChannelRestLength", 0xB011922Fu },
        { "sceAudioSetChannelDataLen", 0xCB2E439Eu },
        // sceSasCore (13)
        { "__sceSasInit", 0x42778A9Fu },
        { "__sceSasCore", 0xA3589D81u },
        { "__sceSasSetVoice", 0x99944089u },
        { "__sceSasSetKeyOn", 0x76F01ACAu },
        { "__sceSasSetKeyOff", 0xA0CF2FA4u },
        { "__sceSasSetPitch", 0xAD84D37Fu },
        { "__sceSasSetVolume", 0x440CA7D8u },
        { "__sceSasSetADSR", 0x019B25EBu },
        { "__sceSasSetSimpleADSR", 0xCBCD4F79u },
        { "__sceSasGetEndFlag", 0x68A46B95u },
        { "__sceSasRevType", 0x33D4AB37u },
        { "__sceSasRevEVOL", 0xD5A229C9u },
        { "__sceSasRevVON", 0xF983B186u },
        // sceAtrac3plus (11)
        { "sceAtracSetDataAndGetID", 0x7A20E7AFu },
        { "sceAtracDecodeData", 0x6A8C3CD5u },
        { "sceAtracGetRemainFrame", 0x9AE849A7u },
        { "sceAtracGetStreamDataInfo", 0x5D268707u },
        { "sceAtracAddStreamData", 0x7DB31251u },
        { "sceAtracSetLoopNum", 0x868120B5u },
        { "sceAtracReleaseAtracID", 0x61EB33F5u },
        { "sceAtracGetSoundSample", 0xA2BBA8BEu },
        { "sceAtracResetPlayPosition", 0x644E5607u },
        { "sceAtracGetBufferInfoForReseting", 0xCA3CA3D2u },
        { "sceAtracGetInternalErrorInfo", 0xE88F759Bu },
    };
    static_assert(sizeof(kAudioNids) / sizeof(kAudioNids[0]) == 30, "Expected 30 audio NIDs");
    for (const auto& item : kAudioNids)
    {
        TEST_ASSERT(ComputeNid(item.name) == item.nid);
        TEST_ASSERT(repsp::runtime::LookupKnownNid(item.nid) != nullptr);
    }

    const auto& known_table = repsp::runtime::GetKnownNidTable();
    TEST_ASSERT(!known_table.empty());
    for (const auto& known_entry : known_table)
    {
        const repsp::runtime::NidNameEntry* found = repsp::runtime::LookupKnownNid(known_entry.first);
        TEST_ASSERT(found != nullptr);
        TEST_ASSERT(found->nid == known_entry.first);
        TEST_ASSERT(found->name == known_entry.second.name);
        TEST_ASSERT(found->library == known_entry.second.library);

        const std::string formatted = repsp::runtime::FormatKnownNid(known_entry.first);
        TEST_ASSERT(formatted.find(known_entry.second.name) != std::string::npos);
        TEST_ASSERT(formatted.find(known_entry.second.library) != std::string::npos);
    }

    // Unknown NID formatting falls back to hex only.
    TEST_ASSERT(repsp::runtime::LookupKnownNid(0x13572468u) == nullptr);
    TEST_ASSERT(repsp::runtime::FormatKnownNid(0x13572468u) == "0x13572468");

    std::printf("nid_probe passed.\n");
    return 0;
}
