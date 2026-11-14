#include "repsp/gpu/ge_interpreter.h"
#include "repsp/gpu/render_backend.h"
#include "repsp/hle/display.h"
#include "repsp/platform/platform.h"
#include "repsp/runtime/guest_memory.h"

#include <cmath>
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

constexpr int kW = 480;
constexpr int kH = 272;
constexpr uint32_t kFbAddr = 0x04000000u;
constexpr uint32_t kZAddr = 0x04100000u;
constexpr uint32_t kTexAddr = 0x04180000u;
constexpr uint32_t kClutAddr = 0x08900000u;

repsp::gpu::Vertex MakeVert(int x, int y, float z, float u, float v, uint32_t rgba)
{
    repsp::gpu::Vertex vert;
    vert.x = x;
    vert.y = y;
    vert.z = z;
    vert.rgba = rgba;
    vert.u = u;
    vert.v = v;
    vert.behind = 0;
    return vert;
}

void RunScene(const char* backend_name, int scale)
{
    TEST_ASSERT(repsp::gpu::SelectRenderBackend(backend_name) == 0);
    repsp::gpu::SetInternalScale(scale);
    const auto* b = repsp::gpu::GetRenderBackend();
    TEST_ASSERT(b != nullptr);

    auto& mem = repsp::runtime::GetGuestMemory();
    std::memset(mem.Translate(kFbAddr), 0, 512u * 272u * 4u);
    std::memset(mem.Translate(kZAddr), 0, 512u * 272u * 2u);

    // 8x8 T4 CLUT texture (swizzled=0, bw=16)
    uint8_t clut[16 * 4] = {};
    auto set_clut = [&](int idx, uint8_t r, uint8_t g, uint8_t b_ch, uint8_t a)
    {
        clut[idx * 4 + 0] = r;
        clut[idx * 4 + 1] = g;
        clut[idx * 4 + 2] = b_ch;
        clut[idx * 4 + 3] = a;
    };
    set_clut(0, 0, 0, 0, 0);
    set_clut(1, 240, 32, 32, 255);
    set_clut(2, 32, 240, 32, 255);
    set_clut(3, 32, 32, 240, 255);
    set_clut(4, 255, 255, 255, 128);
    std::memcpy(mem.Translate(kClutAddr), clut, sizeof(clut));

    uint8_t tex[8 * 8] = {};
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; x += 2)
        {
            const uint8_t p0 = static_cast<uint8_t>(((x + y) & 3) + 1);
            const uint8_t p1 = static_cast<uint8_t>(((x + 1 + y) & 3) + 1);
            tex[y * 8 + (x / 2)] = static_cast<uint8_t>(p0 | (p1 << 4));
        }
    }
    std::memcpy(mem.Translate(kTexAddr), tex, sizeof(tex));

    b->set_target(kFbAddr, 512, 3);

    // 1. Clear mode: dark blue background + depth=65535
    {
        repsp::gpu::DrawState st;
        st.clear = 1;
        st.clear_color = 1;
        st.clear_alpha = 1;
        st.clear_depth = 1;
        st.zaddr = kZAddr;
        st.zbw = 512;
        repsp::gpu::Vertex v[2] = {
            MakeVert(0, 0, 65535.0f, 0, 0, 0xFF302010u),
            MakeVert(480, 272, 65535.0f, 0, 0, 0xFF302010u)
        };
        b->draw(repsp::gpu::kPrimSprites, v, 2, &st);
    }

    // 2. Top-left fill rule test: two adjacent triangles with 50% alpha blend
    {
        repsp::gpu::DrawState st;
        st.gouraud = 0;
        st.blend = 1;
        st.blend_op = 0;
        st.blend_src = 2; // SRC_ALPHA
        st.blend_dst = 3; // ONE_MINUS_SRC_ALPHA
        repsp::gpu::Vertex t1[3] = {
            MakeVert(20, 20, 30000.0f, 0, 0, 0x80FFFFFFu),
            MakeVert(120, 20, 30000.0f, 0, 0, 0x80FFFFFFu),
            MakeVert(20, 120, 30000.0f, 0, 0, 0x800000FFu)
        };
        repsp::gpu::Vertex t2[3] = {
            MakeVert(120, 20, 30000.0f, 0, 0, 0x80FFFFFFu),
            MakeVert(120, 120, 30000.0f, 0, 0, 0x80FFFFFFu),
            MakeVert(20, 120, 30000.0f, 0, 0, 0x8000FF00u)
        };
        b->draw(repsp::gpu::kPrimTriangles, t1, 3, &st);
        b->draw(repsp::gpu::kPrimTriangles, t2, 3, &st);
    }

    // 3. Depth test: closer green triangle vs farther red triangle
    {
        repsp::gpu::DrawState st;
        st.ztest = 1;
        st.zfunc = 4; // LESS
        st.zwrite = 1;
        st.zaddr = kZAddr;
        st.zbw = 512;
        repsp::gpu::Vertex far_tri[3] = {
            MakeVert(150, 30, 40000.0f, 0, 0, 0xFF0000FFu),
            MakeVert(250, 30, 40000.0f, 0, 0, 0xFF0000FFu),
            MakeVert(200, 130, 40000.0f, 0, 0, 0xFF0000FFu)
        };
        repsp::gpu::Vertex near_tri[3] = {
            MakeVert(170, 50, 20000.0f, 0, 0, 0xFF00FF00u),
            MakeVert(270, 50, 20000.0f, 0, 0, 0xFF00FF00u),
            MakeVert(220, 150, 20000.0f, 0, 0, 0xFF00FF00u)
        };
        b->draw(repsp::gpu::kPrimTriangles, far_tri, 3, &st);
        b->draw(repsp::gpu::kPrimTriangles, near_tri, 3, &st);
    }

    // 4. CLUT texture + scissor + alpha test
    {
        repsp::gpu::DrawState st;
        st.tex_enable = 1;
        st.tex_addr = kTexAddr;
        st.tex_w = 8;
        st.tex_h = 8;
        st.tex_bw = 16;
        st.tex_fmt = repsp::gpu::kTpsmT4;
        st.clut = mem.Translate(kClutAddr);
        st.clut_fmt = 3;
        st.clut_mask = 0xF;
        st.tex_func = 0; // modulate
        st.tex_alpha = 1;
        st.sc_x0 = 280;
        st.sc_y0 = 40;
        st.sc_x1 = 360;
        st.sc_y1 = 120;
        st.atest = 1;
        st.atest_func = 6; // GREATER
        st.atest_ref = 150;
        st.atest_mask = 0xFF;
        repsp::gpu::Vertex sp[2] = {
            MakeVert(270, 30, 1000.0f, 0.25f, 0.25f, 0xFFFFFFFFu),
            MakeVert(370, 130, 1000.0f, 16.25f, 16.25f, 0xFFFFFFFFu)
        };
        b->draw(repsp::gpu::kPrimSprites, sp, 2, &st);
    }

    // 5. Color test (discard red)
    {
        repsp::gpu::DrawState st;
        st.ctest = 1;
        st.ctest_func = 3; // NOTEQUAL
        st.ctest_ref = 0x000000FFu;
        st.ctest_mask = 0x00FFFFFFu;
        repsp::gpu::Vertex sp_discard[2] = {
            MakeVert(20, 160, 0.0f, 0, 0, 0xFF0000FFu),
            MakeVert(60, 200, 0.0f, 0, 0, 0xFF0000FFu)
        };
        repsp::gpu::Vertex sp_keep[2] = {
            MakeVert(70, 160, 0.0f, 0, 0, 0xFFFF8000u),
            MakeVert(110, 200, 0.0f, 0, 0, 0xFFFF8000u)
        };
        b->draw(repsp::gpu::kPrimSprites, sp_discard, 2, &st);
        b->draw(repsp::gpu::kPrimSprites, sp_keep, 2, &st);
    }

    if (b->finish)
    {
        b->finish();
    }
}

} // namespace

int main()
{
    TEST_ASSERT(repsp::runtime::GetGuestMemory().Initialize());

    repsp::platform::PlatformConfig pc;
    pc.headless = false;
    pc.display.width = kW;
    pc.display.height = kH;
    pc.display.title = "gl_probe";
    pc.display.dummy = false;
    pc.display.use_opengl = true;
    pc.display.hidden = true;
    pc.audio.dummy = true;

    if (!repsp::platform::Initialize(pc) ||
        repsp::platform::GetDisplay() == nullptr ||
        !repsp::platform::GetDisplay()->HasGlContext())
    {
        std::printf("[gl_probe] SKIP: OpenGL 3.3 Core context unavailable\n");
        repsp::platform::Shutdown();
        return 77;
    }

    RunScene("software", 1);
    std::vector<uint8_t> sw_rgba(static_cast<size_t>(kW) * static_cast<size_t>(kH) * 4u);
    repsp::hle::display::ConvertFrameBufferToRgba8(kFbAddr, 512, 3, sw_rgba.data());

    RunScene("opengl", 1);
    std::vector<uint8_t> gl_rgba;
    int gl_w = 0;
    int gl_h = 0;
    TEST_ASSERT(repsp::gpu::ReadGlTargetRgba8(kFbAddr, 1, gl_rgba, gl_w, gl_h));
    TEST_ASSERT(gl_w == kW && gl_h == kH);
    TEST_ASSERT(gl_rgba.size() == sw_rgba.size());

    int max_diff = 0;
    int mismatch_count = 0;
    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            const size_t idx = (static_cast<size_t>(y) * kW + x) * 4u;
            int px_max = 0;
            for (int c = 0; c < 3; ++c)
            {
                const int d = std::abs(static_cast<int>(sw_rgba[idx + c]) -
                                       static_cast<int>(gl_rgba[idx + c]));
                if (d > px_max) px_max = d;
                if (d > max_diff) max_diff = d;
            }
            if (px_max > 1)
            {
                if (mismatch_count < 10)
                {
                    std::fprintf(stderr,
                                 "[gl_probe] mismatch at (%d,%d): sw=(%u,%u,%u) gl=(%u,%u,%u)\n",
                                 x, y,
                                 sw_rgba[idx + 0], sw_rgba[idx + 1], sw_rgba[idx + 2],
                                 gl_rgba[idx + 0], gl_rgba[idx + 1], gl_rgba[idx + 2]);
                }
                ++mismatch_count;
            }
        }
    }

    std::printf("[gl_probe] scale=1 comparison: max_diff=%d, mismatches(>1)=%d\n",
                max_diff, mismatch_count);
    TEST_ASSERT(mismatch_count == 0);

    // Verify internal scale = 3
    RunScene("opengl", 3);
    std::vector<uint8_t> gl3_rgba;
    int gl3_w = 0;
    int gl3_h = 0;
    TEST_ASSERT(repsp::gpu::ReadGlTargetRgba8(kFbAddr, 3, gl3_rgba, gl3_w, gl3_h));
    TEST_ASSERT(gl3_w == kW * 3 && gl3_h == kH * 3);

    const auto stats = repsp::gpu::GetOpenGlStats();
    TEST_ASSERT(stats.draw_calls > 0);

    TEST_ASSERT(repsp::gpu::SelectRenderBackend("software") == 0);
    repsp::platform::Shutdown();

    // After shutdown (no GL context), selecting opengl must fail and preserve current backend
    TEST_ASSERT(repsp::gpu::SelectRenderBackend("opengl") == -1);
    TEST_ASSERT(repsp::gpu::GetRenderBackend() == &repsp::gpu::kSoftwareBackend);

    std::printf("gl_probe: PASS\n");
    return 0;
}
