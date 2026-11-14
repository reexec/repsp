#include "repsp/gpu/ge_interpreter.h"
#include "repsp/hle/ge.h"
#include "repsp/hle/kernel.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

struct MockCpuState
{
    uint32_t gpr[32] = {0};
    uint32_t fpr[32] = {0};
};

MockCpuState g_cpu;
int g_signal_calls = 0;
int g_finish_calls = 0;
uint32_t g_last_signal_arg0 = 0;
uint32_t g_last_signal_arg1 = 0;
uint32_t g_last_finish_arg0 = 0;
uint32_t g_last_finish_arg1 = 0;

constexpr uint32_t kSignalFuncAddr = 0x08901000u;
constexpr uint32_t kFinishFuncAddr = 0x08901100u;

void MockSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void MockRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t MockGetGpr(int reg) { return (reg >= 0 && reg < 32) ? g_cpu.gpr[reg] : 0; }
void MockSetGpr(int reg, uint32_t val) { if (reg > 0 && reg < 32) g_cpu.gpr[reg] = val; }
uint32_t MockGetFpr(int reg) { return (reg >= 0 && reg < 32) ? g_cpu.fpr[reg] : 0; }
void MockSetFpr(int reg, uint32_t val) { if (reg >= 0 && reg < 32) g_cpu.fpr[reg] = val; }
void MockCallGuest(uint32_t addr)
{
    if (addr == kSignalFuncAddr)
    {
        ++g_signal_calls;
        g_last_signal_arg0 = g_cpu.gpr[4];
        g_last_signal_arg1 = g_cpu.gpr[5];
    }
    else if (addr == kFinishFuncAddr)
    {
        ++g_finish_calls;
        g_last_finish_arg0 = g_cpu.gpr[4];
        g_last_finish_arg1 = g_cpu.gpr[5];
    }
}

repsp_guest_cpu_ops MakeMockOps()
{
    repsp_guest_cpu_ops ops{};
    ops.context_size = sizeof(MockCpuState);
    ops.save = MockSave;
    ops.restore = MockRestore;
    ops.get_gpr = MockGetGpr;
    ops.set_gpr = MockSetGpr;
    ops.call_guest = MockCallGuest;
    ops.get_fpr = MockGetFpr;
    ops.set_fpr = MockSetFpr;
    return ops;
}

uint32_t InvokeNid(repsp::runtime::NidRegistry& reg, uint32_t nid,
                   uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0)
{
    g_cpu.gpr[4] = a0;
    g_cpu.gpr[5] = a1;
    g_cpu.gpr[6] = a2;
    g_cpu.gpr[7] = a3;
    TEST_ASSERT(reg.Invoke(nid));
    return g_cpu.gpr[2];
}

constexpr uint32_t GeCmd(uint8_t op, uint32_t arg)
{
    return (static_cast<uint32_t>(op) << 24) | (arg & 0x00FFFFFFu);
}

// GE opcodes
constexpr uint8_t kOpVaddr = 0x01;
constexpr uint8_t kOpPrim = 0x04;
constexpr uint8_t kOpEnd = 0x0C;
constexpr uint8_t kOpSignal = 0x0E;
constexpr uint8_t kOpFinish = 0x0F;
constexpr uint8_t kOpBase = 0x10;
constexpr uint8_t kOpVtype = 0x12;
constexpr uint8_t kOpTme = 0x1E;
constexpr uint8_t kOpShade = 0x50;
constexpr uint8_t kOpFbp = 0x9C;
constexpr uint8_t kOpFbw = 0x9D;
constexpr uint8_t kOpTbp0 = 0xA0;
constexpr uint8_t kOpTbw0 = 0xA8;
constexpr uint8_t kOpCbp = 0xB0;
constexpr uint8_t kOpCbph = 0xB1;
constexpr uint8_t kOpTsize0 = 0xB8;
constexpr uint8_t kOpTmode = 0xC2;
constexpr uint8_t kOpTpsm = 0xC3;
constexpr uint8_t kOpCload = 0xC4;
constexpr uint8_t kOpCmode = 0xC5;
constexpr uint8_t kOpTfunc = 0xC9;
constexpr uint8_t kOpPsm = 0xD2;
constexpr uint8_t kOpClear = 0xD3;
constexpr uint8_t kOpScissor1 = 0xD4;
constexpr uint8_t kOpScissor2 = 0xD5;

// NIDs
constexpr uint32_t kNidGeEdramGetAddr = 0xE47E40E4u;
constexpr uint32_t kNidGeSetCallback = 0xA4FC06A4u;
constexpr uint32_t kNidGeUnsetCallback = 0x05DB22CEu;
constexpr uint32_t kNidGeListEnQueue = 0xAB49E76Au;
constexpr uint32_t kNidGeListUpdateStallAddr = 0xE0D68148u;
constexpr uint32_t kNidGeListSync = 0x03444EB4u;
constexpr uint32_t kNidGeDrawSync = 0xB287BD61u;

struct VertexColorPos2D
{
    uint32_t color;
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t pad;
};

struct VertexTexColorPos2D
{
    int16_t u;
    int16_t v;
    uint32_t color;
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t pad;
};

void WriteList(uint32_t addr, const std::vector<uint32_t>& cmds)
{
    for (size_t i = 0; i < cmds.size(); ++i)
    {
        repsp_guest_write32(addr + static_cast<uint32_t>(i * 4), cmds[i]);
    }
}

uint32_t ReadVramPixel32(uint32_t fbp, uint32_t fbw, int x, int y)
{
    return repsp_guest_read32(fbp + static_cast<uint32_t>((y * static_cast<int>(fbw) + x) * 4));
}

} // namespace

int main()
{
    auto& mem = repsp::runtime::GetGuestMemory();
    TEST_ASSERT(mem.Initialize());

    auto& nids = repsp::runtime::GetNidRegistry();
    auto& sched = repsp::runtime::GetScheduler();
    repsp_guest_cpu_ops ops = MakeMockOps();
    repsp_recomp_register_cpu(&ops);
    repsp::hle::kernel::Register(repsp::hle::kernel::KernelConfig{});
    repsp::hle::ge::Register();

    // 1. sceGeEdramGetAddr
    const uint32_t edram = InvokeNid(nids, kNidGeEdramGetAddr);
    TEST_ASSERT(edram == 0x04000000u);

    constexpr uint32_t kFbAddr = 0x04000000u;
    constexpr uint32_t kFbw = 512u;
    constexpr uint32_t kListAddr = 0x08910000u;
    constexpr uint32_t kVertAddr = 0x08920000u;

    // 2. Clear + flat sprite
    {
        const VertexColorPos2D verts[4] = {
            { 0xFF102030u, 0, 0, 0, 0 },
            { 0xFF102030u, 64, 64, 0, 0 },
            { 0xFF445566u, 10, 10, 0, 0 },
            { 0xFF445566u, 20, 20, 0, 0 },
        };
        std::memcpy(mem.Translate(kVertAddr), verts, sizeof(verts));

        const std::vector<uint32_t> cmds = {
            GeCmd(kOpFbp, kFbAddr & 0x00FFFFFFu),
            GeCmd(kOpFbw, ((kFbAddr >> 8) & 0x00FF0000u) | kFbw),
            GeCmd(kOpPsm, 3), // 8888
            GeCmd(kOpScissor1, 0),
            GeCmd(kOpScissor2, (271u << 10) | 479u),
            // VTYPE: THROUGH (bit 23) | POS16 (2 << 7) | COL8888 (7 << 2)
            GeCmd(kOpVtype, (1u << 23) | (2u << 7) | (7u << 2)),
            GeCmd(kOpBase, (kVertAddr >> 8) & 0x00FF0000u),
            GeCmd(kOpVaddr, kVertAddr & 0x00FFFFFFu),
            GeCmd(kOpClear, 0x0301u), // clear + clear_color + clear_alpha
            GeCmd(kOpPrim, (6u << 16) | 2u), // sprites, 2 verts
            GeCmd(kOpClear, 0),
            GeCmd(kOpPrim, (6u << 16) | 2u), // flat sprite, next 2 verts
            GeCmd(kOpFinish, 0),
            GeCmd(kOpEnd, 0),
        };
        WriteList(kListAddr, cmds);

        const int32_t list_id = static_cast<int32_t>(
            InvokeNid(nids, kNidGeListEnQueue, kListAddr, 0, static_cast<uint32_t>(-1), 0));
        TEST_ASSERT(list_id > 0);
        TEST_ASSERT(InvokeNid(nids, kNidGeListSync, static_cast<uint32_t>(list_id), 1) == 0);
        TEST_ASSERT(InvokeNid(nids, kNidGeDrawSync, 0) == 0);

        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 5, 5) == 0xFF102030u);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 15, 15) == 0xFF445566u);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 25, 25) == 0xFF102030u);
    }

    // 3. Vertex-colored Gouraud triangle
    {
        const VertexColorPos2D tri[3] = {
            { 0xFF0000FFu, 30, 10, 0, 0 }, // red
            { 0xFF00FF00u, 50, 10, 0, 0 }, // green
            { 0xFFFF0000u, 40, 30, 0, 0 }, // blue
        };
        std::memcpy(mem.Translate(kVertAddr), tri, sizeof(tri));

        const std::vector<uint32_t> cmds = {
            GeCmd(kOpShade, 1),
            GeCmd(kOpVtype, (1u << 23) | (2u << 7) | (7u << 2)),
            GeCmd(kOpBase, (kVertAddr >> 8) & 0x00FF0000u),
            GeCmd(kOpVaddr, kVertAddr & 0x00FFFFFFu),
            GeCmd(kOpPrim, (3u << 16) | 3u), // triangles, 3 verts
            GeCmd(kOpShade, 0),
            GeCmd(kOpFinish, 0),
            GeCmd(kOpEnd, 0),
        };
        WriteList(kListAddr, cmds);
        const int32_t list_id = static_cast<int32_t>(
            InvokeNid(nids, kNidGeListEnQueue, kListAddr, 0, static_cast<uint32_t>(-1), 0));
        TEST_ASSERT(list_id > 0);
        const uint32_t interior = ReadVramPixel32(kFbAddr, kFbw, 40, 16);
        TEST_ASSERT(interior != 0xFF102030u);
        TEST_ASSERT((interior & 0x000000FFu) > 0 && (interior & 0x0000FF00u) > 0 && (interior & 0x00FF0000u) > 0);
    }

    // 4. Textured sprites: 8888 linear, 4444 linear, T8 CLUT linear & swizzled
    {
        constexpr uint32_t kTexAddr = 0x08930000u;
        constexpr uint32_t kClutAddr = 0x08940000u;

        // 4a. 8888 linear 16x8 texture (bw=16)
        std::vector<uint32_t> tex8888(16 * 8, 0xFFABCDEFu);
        tex8888[0] = 0xFF112233u;
        tex8888[3 * 16 + 4] = 0xFF778899u;
        std::memcpy(mem.Translate(kTexAddr), tex8888.data(), tex8888.size() * 4);

        const VertexTexColorPos2D qverts[2] = {
            { 0, 0, 0xFFFFFFFFu, 0, 40, 0, 0 },
            { 16, 8, 0xFFFFFFFFu, 16, 48, 0, 0 },
        };
        std::memcpy(mem.Translate(kVertAddr), qverts, sizeof(qverts));

        const std::vector<uint32_t> cmds = {
            GeCmd(kOpTme, 1),
            GeCmd(kOpTmode, 0), // linear
            GeCmd(kOpTpsm, 3),  // 8888
            GeCmd(kOpTbp0, kTexAddr & 0x00FFFFFFu),
            GeCmd(kOpTbw0, ((kTexAddr >> 8) & 0x00FF0000u) | 16u),
            GeCmd(kOpTsize0, (3u << 8) | 4u), // 16x8
            GeCmd(kOpTfunc, 3), // REPLACE
            GeCmd(kOpVtype, (1u << 23) | (2u << 7) | (7u << 2) | 2u), // TEX16 | COL8888 | POS16 | THROUGH
            GeCmd(kOpBase, (kVertAddr >> 8) & 0x00FF0000u),
            GeCmd(kOpVaddr, kVertAddr & 0x00FFFFFFu),
            GeCmd(kOpPrim, (6u << 16) | 2u),
            GeCmd(kOpTme, 0),
            GeCmd(kOpFinish, 0),
            GeCmd(kOpEnd, 0),
        };
        WriteList(kListAddr, cmds);
        TEST_ASSERT(static_cast<int32_t>(InvokeNid(nids, kNidGeListEnQueue, kListAddr, 0, static_cast<uint32_t>(-1), 0)) > 0);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 0, 40) == 0xFF112233u);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 4, 43) == 0xFF778899u);

        // 4b. 4444 linear 16x8 texture
        std::vector<uint16_t> tex4444(16 * 8, 0xFFFFu);
        tex4444[2 * 16 + 5] = 0xF123u; // A=0xF, B=0x1, G=0x2, R=0x3 -> 0xFF112233
        std::memcpy(mem.Translate(kTexAddr), tex4444.data(), tex4444.size() * 2);
        const std::vector<uint32_t> cmds4444 = {
            GeCmd(kOpTme, 1),
            GeCmd(kOpTmode, 0),
            GeCmd(kOpTpsm, 2), // 4444
            GeCmd(kOpTbp0, kTexAddr & 0x00FFFFFFu),
            GeCmd(kOpTbw0, ((kTexAddr >> 8) & 0x00FF0000u) | 16u),
            GeCmd(kOpTsize0, (3u << 8) | 4u),
            GeCmd(kOpTfunc, 3),
            GeCmd(kOpVtype, (1u << 23) | (2u << 7) | (7u << 2) | 2u),
            GeCmd(kOpBase, (kVertAddr >> 8) & 0x00FF0000u),
            GeCmd(kOpVaddr, kVertAddr & 0x00FFFFFFu),
            GeCmd(kOpPrim, (6u << 16) | 2u),
            GeCmd(kOpTme, 0),
            GeCmd(kOpFinish, 0),
            GeCmd(kOpEnd, 0),
        };
        WriteList(kListAddr, cmds4444);
        TEST_ASSERT(static_cast<int32_t>(InvokeNid(nids, kNidGeListEnQueue, kListAddr, 0, static_cast<uint32_t>(-1), 0)) > 0);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 5, 42) == 0xFF112233u);

        // 4c. T8 CLUT swizzled 16x8 texture (16 bytes wide x 8 rows = 1 swizzle block)
        uint32_t clut[256] = {0};
        clut[42] = 0xFFCCBBAAu;
        clut[99] = 0xFF334455u;
        std::memcpy(mem.Translate(kClutAddr), clut, sizeof(clut));

        std::vector<uint8_t> tex_t8(16 * 8, 42);
        // In 16-byte x 8-row swizzle, linear (x=7, y=5) is in block 0 at offset y*16 + x.
        tex_t8[5 * 16 + 7] = 99;
        std::memcpy(mem.Translate(kTexAddr), tex_t8.data(), tex_t8.size());

        const std::vector<uint32_t> cmds_t8 = {
            GeCmd(kOpCbp, kClutAddr & 0x00FFFFFFu),
            GeCmd(kOpCbph, (kClutAddr >> 8) & 0x00FF0000u),
            GeCmd(kOpCmode, 3u | (0xFFu << 8)), // 8888 CLUT, mask 0xFF
            GeCmd(kOpCload, 256u / 8u),        // 32 blocks of 8 entries
            GeCmd(kOpTme, 1),
            GeCmd(kOpTmode, 1),                // swizzled
            GeCmd(kOpTpsm, 5),                 // T8
            GeCmd(kOpTbp0, kTexAddr & 0x00FFFFFFu),
            GeCmd(kOpTbw0, ((kTexAddr >> 8) & 0x00FF0000u) | 16u),
            GeCmd(kOpTsize0, (3u << 8) | 4u),
            GeCmd(kOpTfunc, 3),
            GeCmd(kOpVtype, (1u << 23) | (2u << 7) | (7u << 2) | 2u),
            GeCmd(kOpBase, (kVertAddr >> 8) & 0x00FF0000u),
            GeCmd(kOpVaddr, kVertAddr & 0x00FFFFFFu),
            GeCmd(kOpPrim, (6u << 16) | 2u),
            GeCmd(kOpTme, 0),
            GeCmd(kOpFinish, 0),
            GeCmd(kOpEnd, 0),
        };
        WriteList(kListAddr, cmds_t8);
        TEST_ASSERT(static_cast<int32_t>(InvokeNid(nids, kNidGeListEnQueue, kListAddr, 0, static_cast<uint32_t>(-1), 0)) > 0);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 0, 40) == 0xFFCCBBAAu);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 7, 45) == 0xFF334455u);
    }

    // 5. Stall stop + UpdateStallAddr resume + SIGNAL/FINISH callbacks
    {
        constexpr uint32_t kCbDataAddr = 0x08950000u;
        repsp_guest_write32(kCbDataAddr + 0, kSignalFuncAddr);
        repsp_guest_write32(kCbDataAddr + 4, 0x11110001u);
        repsp_guest_write32(kCbDataAddr + 8, kFinishFuncAddr);
        repsp_guest_write32(kCbDataAddr + 12, 0x22220002u);

        const int32_t cb_id = static_cast<int32_t>(
            InvokeNid(nids, kNidGeSetCallback, kCbDataAddr));
        TEST_ASSERT(cb_id >= 0);

        const VertexColorPos2D post_stall[2] = {
            { 0xFFCAFE01u, 100, 100, 0, 0 },
            { 0xFFCAFE01u, 110, 110, 0, 0 },
        };
        std::memcpy(mem.Translate(kVertAddr), post_stall, sizeof(post_stall));

        const std::vector<uint32_t> cmds = {
            GeCmd(kOpSignal, 0x00001234u),
            GeCmd(kOpVtype, (1u << 23) | (2u << 7) | (7u << 2)),
            GeCmd(kOpBase, (kVertAddr >> 8) & 0x00FF0000u),
            GeCmd(kOpVaddr, kVertAddr & 0x00FFFFFFu), // index 3 -> stall at index 4 (16 bytes)
            GeCmd(kOpPrim, (6u << 16) | 2u),
            GeCmd(kOpFinish, 0x00005678u),
            GeCmd(kOpEnd, 0),
        };
        WriteList(kListAddr, cmds);

        const uint32_t stall1 = kListAddr + 4u * 4u;
        const uint32_t stall_end = kListAddr + static_cast<uint32_t>(cmds.size() * 4u);

        const int32_t list_id = static_cast<int32_t>(
            InvokeNid(nids, kNidGeListEnQueue, kListAddr, stall1, static_cast<uint32_t>(cb_id), 0));
        TEST_ASSERT(list_id > 0);
        TEST_ASSERT(InvokeNid(nids, kNidGeListSync, static_cast<uint32_t>(list_id), 1) ==
                    repsp::gpu::kGeListStalled);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 105, 105) != 0xFFCAFE01u);

        TEST_ASSERT(InvokeNid(nids, kNidGeListUpdateStallAddr, static_cast<uint32_t>(list_id), stall_end) == 0);
        TEST_ASSERT(InvokeNid(nids, kNidGeListSync, static_cast<uint32_t>(list_id), 1) ==
                    repsp::gpu::kGeListDone);
        TEST_ASSERT(InvokeNid(nids, kNidGeListSync, static_cast<uint32_t>(list_id), 0) == 0);
        TEST_ASSERT(InvokeNid(nids, kNidGeDrawSync, 1) == repsp::gpu::kGeListDone);
        TEST_ASSERT(ReadVramPixel32(kFbAddr, kFbw, 105, 105) == 0xFFCAFE01u);

        sched.DeliverPendingInterrupts();
        TEST_ASSERT(g_signal_calls == 1);
        TEST_ASSERT(g_finish_calls == 1);
        TEST_ASSERT(g_last_signal_arg0 == 0x1234u);
        TEST_ASSERT(g_last_signal_arg1 == 0x11110001u);
        TEST_ASSERT(g_last_finish_arg0 == 0x5678u);
        TEST_ASSERT(g_last_finish_arg1 == 0x22220002u);

        TEST_ASSERT(InvokeNid(nids, kNidGeUnsetCallback, static_cast<uint32_t>(cb_id)) == 0);
    }

    std::printf("ge_probe passed.\n");
    return 0;
}
