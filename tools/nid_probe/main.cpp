#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/sha1.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
    using namespace repsp::runtime;

    // 1. Verify NID calculation: first 4 bytes of SHA-1 in little-endian
    const char func_name[] = "sceKernelExitGame";
    uint8_t digest[20];
    ComputeSha1(reinterpret_cast<const uint8_t*>(func_name), sizeof(func_name) - 1, digest);
    uint32_t expected_nid = static_cast<uint32_t>(digest[0]) |
                            (static_cast<uint32_t>(digest[1]) << 8) |
                            (static_cast<uint32_t>(digest[2]) << 16) |
                            (static_cast<uint32_t>(digest[3]) << 24);

    uint32_t calculated_nid = ComputeNid(func_name);
    TEST_ASSERT(calculated_nid == expected_nid);

    // 2. Test NID registry registration and invocation
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

    // 3. Test unimplemented NID invocation
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
