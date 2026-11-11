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

    std::printf("nid_probe passed.\n");
    return 0;
}
