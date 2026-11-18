#include "repsp/gpu/ge_interpreter.h"
#include "repsp/gpu/render_backend.h"
#include "repsp/hle/display.h"
#include "repsp/platform/platform.h"
#include "repsp/platform/post_shader.h"
#include "repsp/runtime/guest_memory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#define TEST_ASSERT(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::fflush(nullptr); \
            std::_Exit(1); \
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

    // Verify User RAM non-CLUT texture invalidation at the same guest address
    {
        constexpr uint32_t kUserTexAddr = 0x08900000u;
        auto& mem = repsp::runtime::GetGuestMemory();
        uint32_t* tex_ptr = reinterpret_cast<uint32_t*>(mem.Translate(kUserTexAddr));
        auto* b = repsp::gpu::GetRenderBackend();
        TEST_ASSERT(b == &repsp::gpu::kOpenGlBackend);
        b->set_target(kFbAddr, 512, 3);

        auto draw_user_tex = [&](uint32_t abgr)
        {
            for (int i = 0; i < 16 * 16; ++i)
            {
                tex_ptr[i] = abgr;
            }
            repsp::gpu::DrawState st;
            st.tex_enable = 1;
            st.tex_addr = kUserTexAddr;
            st.tex_bw = 16;
            st.tex_w = 16;
            st.tex_h = 16;
            st.tex_fmt = 3; // ABGR8888
            st.tex_func = 3; // REPLACE
            repsp::gpu::Vertex v[2] = {
                MakeVert(0, 0, 0.0f, 0, 0, 0xFFFFFFFFu),
                MakeVert(16, 16, 0.0f, 16, 16, 0xFFFFFFFFu)
            };
            b->draw(repsp::gpu::kPrimSprites, v, 2, &st);
            b->finish();
        };

        std::vector<uint8_t> tex_rgba;
        int tw = 0;
        int th = 0;
        draw_user_tex(0xFF0000FFu); // Red
        TEST_ASSERT(repsp::gpu::ReadGlTargetRgba8(kFbAddr, 1, tex_rgba, tw, th));
        size_t idx = (8u * static_cast<size_t>(tw) + 8u) * 4u;
        TEST_ASSERT(tex_rgba[idx + 0] >= 240 && tex_rgba[idx + 1] <= 15 && tex_rgba[idx + 2] <= 15);

        draw_user_tex(0xFF00FF00u); // Green at the exact same address/format/size
        TEST_ASSERT(repsp::gpu::ReadGlTargetRgba8(kFbAddr, 1, tex_rgba, tw, th));
        TEST_ASSERT(tex_rgba[idx + 0] <= 15 && tex_rgba[idx + 1] >= 240 && tex_rgba[idx + 2] <= 15);
    }

    // Verify repeated draws of the same texture within one flush do not re-hash texture RAM
    {
        constexpr uint32_t kLargeTexAddr = 0x08A00000u;
        constexpr uint32_t kLargeTexBytes = 512u * 512u * 4u;
        auto& mem = repsp::runtime::GetGuestMemory();
        uint32_t* tex_ptr = reinterpret_cast<uint32_t*>(mem.Translate(kLargeTexAddr));
        std::fill_n(tex_ptr, 512u * 512u, 0xFFFF0000u); // Blue

        auto* b = repsp::gpu::GetRenderBackend();
        TEST_ASSERT(b == &repsp::gpu::kOpenGlBackend);
        b->set_target(kFbAddr, 512, 3);

        repsp::gpu::DrawState st;
        st.tex_enable = 1;
        st.tex_addr = kLargeTexAddr;
        st.tex_bw = 512;
        st.tex_w = 512;
        st.tex_h = 512;
        st.tex_fmt = 3; // ABGR8888
        st.tex_func = 3; // REPLACE
        repsp::gpu::Vertex v[2] = {
            MakeVert(0, 0, 0.0f, 0, 0, 0xFFFFFFFFu),
            MakeVert(16, 16, 0.0f, 16, 16, 0xFFFFFFFFu)
        };

        repsp::gpu::ResetOpenGlStats();
        for (int i = 0; i < 32; ++i)
        {
            b->draw(repsp::gpu::kPrimSprites, v, 2, &st);
        }
        b->finish();
        const auto s1 = repsp::gpu::GetOpenGlStats();
        TEST_ASSERT(s1.texture_bytes_hashed == kLargeTexBytes);
        TEST_ASSERT(s1.texture_cache_hits == 31);

        // Next flush must re-hash once and pick up User RAM overwrite
        std::fill_n(tex_ptr, 512u * 512u, 0xFF0000FFu); // Red
        b->draw(repsp::gpu::kPrimSprites, v, 2, &st);
        b->finish();
        const auto s2 = repsp::gpu::GetOpenGlStats();
        TEST_ASSERT(s2.texture_bytes_hashed == kLargeTexBytes * 2u);

        // Guest code running between two draws of one flush (the CPU between
        // stalls of one list, or another guest thread at a yield) must also
        // force a re-hash, so a texture rewritten in between is seen.
        b->draw(repsp::gpu::kPrimSprites, v, 2, &st);
        TEST_ASSERT(b->guest_memory_may_have_changed != nullptr);
        b->guest_memory_may_have_changed();
        b->draw(repsp::gpu::kPrimSprites, v, 2, &st);
        b->finish();
        const auto s3 = repsp::gpu::GetOpenGlStats();
        TEST_ASSERT(s3.texture_bytes_hashed == kLargeTexBytes * 4u);

        std::vector<uint8_t> tex_rgba;
        int tw = 0;
        int th = 0;
        TEST_ASSERT(repsp::gpu::ReadGlTargetRgba8(kFbAddr, 1, tex_rgba, tw, th));
        const size_t idx = (8u * static_cast<size_t>(tw) + 8u) * 4u;
        TEST_ASSERT(tex_rgba[idx + 0] >= 240 && tex_rgba[idx + 1] <= 15 && tex_rgba[idx + 2] <= 15);
    }

    // Verify MSAA render-to-texture resolve and texture filtering / GPU memory queries
    {
        constexpr uint32_t kRttSrcAddr = 0x04088000u;
        // 512x272 ABGR8888 targets take 0x88000 bytes each; the two must not overlap,
        // or which one a texture read finds depends on the target map's order.
        constexpr uint32_t kRttDstAddr = 0x04110000u;
        auto* b = repsp::gpu::GetRenderBackend();
        TEST_ASSERT(b == &repsp::gpu::kOpenGlBackend);

        repsp::gpu::SetInternalScale(2);
        repsp::gpu::SetMsaaSamples(4);
        TEST_ASSERT(repsp::gpu::GetMsaaSamples() == 4);
        repsp::gpu::SetTextureFilterMode(repsp::gpu::TextureFilterMode::kAniso4x);
        TEST_ASSERT(repsp::gpu::GetTextureFilterMode() == repsp::gpu::TextureFilterMode::kAniso4x);

        // Draw green solid into kRttSrcAddr under MSAA, then sample it into kRttDstAddr
        b->set_target(kRttSrcAddr, 512, 3);
        repsp::gpu::DrawState clear_st;
        clear_st.clear = 1;
        clear_st.clear_color = 1;
        clear_st.clear_alpha = 1;
        repsp::gpu::Vertex cv[2] = {
            MakeVert(0, 0, 0.0f, 0, 0, 0xFF00FF00u),
            MakeVert(480, 272, 0.0f, 0, 0, 0xFF00FF00u)
        };
        b->draw(repsp::gpu::kPrimSprites, cv, 2, &clear_st);

        b->set_target(kRttDstAddr, 512, 3);
        repsp::gpu::DrawState rtt_st;
        rtt_st.tex_enable = 1;
        rtt_st.tex_addr = kRttSrcAddr;
        rtt_st.tex_bw = 512;
        rtt_st.tex_w = 480;
        rtt_st.tex_h = 272;
        rtt_st.tex_fmt = 3;
        rtt_st.tex_func = 3; // REPLACE
        repsp::gpu::Vertex rv[2] = {
            MakeVert(0, 0, 0.0f, 0, 0, 0xFFFFFFFFu),
            MakeVert(480, 272, 0.0f, 480, 272, 0xFFFFFFFFu)
        };
        b->draw(repsp::gpu::kPrimSprites, rv, 2, &rtt_st);
        b->finish();

        std::vector<uint8_t> dst_rgba;
        int dw = 0;
        int dh = 0;
        TEST_ASSERT(repsp::gpu::ReadGlTargetRgba8(kRttDstAddr, 1, dst_rgba, dw, dh));
        const size_t mid = (136u * static_cast<size_t>(dw) + 240u) * 4u;
        TEST_ASSERT(dst_rgba[mid + 0] <= 15 && dst_rgba[mid + 1] >= 240 && dst_rgba[mid + 2] <= 15);

        const auto mem_info = repsp::gpu::GetGpuMemoryInfo();
        TEST_ASSERT(mem_info.hle_targets_bytes > 0);

        repsp::gpu::SetMsaaSamples(0);
        repsp::gpu::SetTextureFilterMode(repsp::gpu::TextureFilterMode::kOriginal);
        repsp::gpu::SetInternalScale(1);
    }

    TEST_ASSERT(repsp::gpu::SelectRenderBackend("software") == 0);
    repsp::platform::Shutdown();

    // After shutdown (no GL context), selecting opengl must fail and preserve current backend
    TEST_ASSERT(repsp::gpu::SelectRenderBackend("opengl") == -1);
    TEST_ASSERT(repsp::gpu::GetRenderBackend() == &repsp::gpu::kSoftwareBackend);

    // Post-processing screen shaders on 2x hidden OpenGL 3.3 Core window
    {
        std::error_code ec;
        const auto shader_dir = std::filesystem::temp_directory_path(ec) / "repsp_gl_post_shader_probe";
        std::filesystem::remove_all(shader_dir, ec);
        std::filesystem::create_directories(shader_dir, ec);

        // Two-pass preset: pass 0 writes green, pass 1 swaps G->B so output is blue
        {
            std::ofstream(shader_dir / "p0.glsl") <<
                "#if defined(VERTEX)\n"
                "attribute vec4 VertexCoord;\n"
                "attribute vec4 TexCoord;\n"
                "uniform mat4 MVPMatrix;\n"
                "varying vec2 uv;\n"
                "void main() { gl_Position = MVPMatrix * VertexCoord; uv = TexCoord.xy; }\n"
                "#elif defined(FRAGMENT)\n"
                "uniform sampler2D Texture;\n"
                "varying vec2 uv;\n"
                "void main() { vec4 c = texture2D(Texture, uv); gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }\n"
                "#endif\n";
            std::ofstream(shader_dir / "p1.glsl") <<
                "#if defined(VERTEX)\n"
                "attribute vec4 VertexCoord;\n"
                "attribute vec4 TexCoord;\n"
                "uniform mat4 MVPMatrix;\n"
                "varying vec2 uv;\n"
                "void main() { gl_Position = MVPMatrix * VertexCoord; uv = TexCoord.xy; }\n"
                "#elif defined(FRAGMENT)\n"
                "uniform sampler2D Texture;\n"
                "varying vec2 uv;\n"
                "void main() { vec4 c = texture2D(Texture, uv); gl_FragColor = vec4(0.0, 0.0, c.g, 1.0); }\n"
                "#endif\n";
            std::ofstream(shader_dir / "two_pass.glslp") <<
                "shaders = 2\n"
                "shader0 = p0.glsl\n"
                "scale_type0 = source\n"
                "scale0 = 1.0\n"
                "shader1 = p1.glsl\n";
            std::ofstream(shader_dir / "broken.glsl") <<
                "#if defined(VERTEX)\n"
                "void main() { broken_syntax_here }\n"
                "#elif defined(FRAGMENT)\n"
                "void main() { gl_FragColor = vec4(1.0); }\n"
                "#endif\n";
        }

        repsp::platform::SetPostShaderDirectory(shader_dir);
        repsp::platform::SetInitialPostShader("none");

        repsp::platform::PlatformConfig p2;
        p2.headless = false;
        p2.display.width = kW * 2;
        p2.display.height = kH * 2;
        p2.display.title = "gl_probe_post";
        p2.display.dummy = false;
        p2.display.use_opengl = true;
        p2.display.hidden = true;
        p2.audio.dummy = true;
        TEST_ASSERT(repsp::platform::Initialize(p2));
        TEST_ASSERT(repsp::gpu::SelectRenderBackend("opengl") == 0);
        repsp::gpu::SetInternalScale(2);

        auto* display = repsp::platform::GetDisplay();
        TEST_ASSERT(display != nullptr && display->HasGlContext());

        // 1. All 5 built-ins compile and link on GL 3.3 Core; broken falls back to none
        display->SubmitRenderJob([]()
        {
            auto* ctrl = repsp::platform::GetPostShaderControl();
            TEST_ASSERT(ctrl != nullptr);
            const char* kBuiltins[] = {"crt", "scanline", "fsr", "scalefx", "anime4k"};
            for (const char* id : kBuiltins)
            {
                TEST_ASSERT(ctrl->Select(id));
                TEST_ASSERT(ctrl->active_id() == id);
            }
            TEST_ASSERT(!ctrl->Select("broken.glsl"));
            TEST_ASSERT(ctrl->active_id() == "none");
            TEST_ASSERT(!ctrl->last_error().empty());
        });
        display->WaitRenderIdle();

        // Prepare white target at scale 2
        auto* b = repsp::gpu::GetRenderBackend();
        b->set_target(kFbAddr, 512, 3);
        repsp::gpu::DrawState clear_st;
        clear_st.clear = 1;
        clear_st.clear_color = 1;
        clear_st.clear_alpha = 1;
        repsp::gpu::Vertex cv[2] = {
            MakeVert(0, 0, 0.0f, 0, 0, 0xFFFFFFFFu),
            MakeVert(kW, kH, 0.0f, 0, 0, 0xFFFFFFFFu)
        };
        b->draw(repsp::gpu::kPrimSprites, cv, 2, &clear_st);
        b->finish();

        using ReadPixelsFn = void (*)(int, int, int, int, unsigned int, unsigned int, void*);
        using BindFbFn = void (*)(unsigned int, unsigned int);
        auto read_default_fb = [&](std::vector<uint8_t>& out)
        {
            out.resize(static_cast<size_t>(kW * 2) * static_cast<size_t>(kH * 2) * 4u);
            display->SubmitRenderJob([&]()
            {
                ReadPixelsFn gl_read_pixels = nullptr;
                BindFbFn gl_bind_fb = nullptr;
                void* p_rp = display->GetGlProcAddress("glReadPixels");
                void* p_bf = display->GetGlProcAddress("glBindFramebuffer");
                TEST_ASSERT(p_rp != nullptr && p_bf != nullptr);
                std::memcpy(&gl_read_pixels, &p_rp, sizeof(gl_read_pixels));
                std::memcpy(&gl_bind_fb, &p_bf, sizeof(gl_bind_fb));
                gl_bind_fb(0x8CA8u /*GL_READ_FRAMEBUFFER*/, 0);
                gl_read_pixels(0, 0, kW * 2, kH * 2, 0x1908u /*GL_RGBA*/, 0x1401u /*GL_UNSIGNED_BYTE*/, out.data());
            });
            display->WaitRenderIdle();
        };

        // 2. `none` produces exact white pixels matching no-shader draw
        std::vector<uint8_t> none_fb;
        TEST_ASSERT(repsp::gpu::PresentGlTarget(kFbAddr));
        TEST_ASSERT(repsp::gpu::PresentGlTarget(kFbAddr));
        read_default_fb(none_fb);
        const size_t center = (static_cast<size_t>(kH) * static_cast<size_t>(kW * 2) + static_cast<size_t>(kW)) * 4u;
        TEST_ASSERT(none_fb[center + 0] >= 240 && none_fb[center + 1] >= 240 && none_fb[center + 2] >= 240);

        // 3. `scanline` darkens alternating rows against 272 logical lines even at internal scale 2
        display->SubmitRenderJob([]()
        {
            TEST_ASSERT(repsp::platform::GetPostShaderControl()->Select("scanline"));
        });
        display->WaitRenderIdle();
        TEST_ASSERT(repsp::gpu::PresentGlTarget(kFbAddr));
        TEST_ASSERT(repsp::gpu::PresentGlTarget(kFbAddr));
        std::vector<uint8_t> scan_fb;
        read_default_fb(scan_fb);
        const uint8_t row_a = scan_fb[(100u * static_cast<size_t>(kW * 2) + 100u) * 4u];
        const uint8_t row_b = scan_fb[(101u * static_cast<size_t>(kW * 2) + 100u) * 4u];
        TEST_ASSERT((row_a > row_b + 30) || (row_b > row_a + 30));

        // 4. Two-pass preset chains (pass0 -> green, pass1 -> blue)
        display->SubmitRenderJob([]()
        {
            TEST_ASSERT(repsp::platform::GetPostShaderControl()->Select("two_pass.glslp"));
        });
        display->WaitRenderIdle();
        TEST_ASSERT(repsp::gpu::PresentGlTarget(kFbAddr));
        TEST_ASSERT(repsp::gpu::PresentGlTarget(kFbAddr));
        std::vector<uint8_t> two_fb;
        read_default_fb(two_fb);
        TEST_ASSERT(two_fb[center + 0] <= 15 && two_fb[center + 1] <= 15 && two_fb[center + 2] >= 240);

        // 5. `flip_y` input (PresentRgba8) stays right-way up under a shader (top red, bottom blue)
        display->SubmitRenderJob([]()
        {
            TEST_ASSERT(repsp::platform::GetPostShaderControl()->Select("scanline"));
        });
        display->WaitRenderIdle();
        std::vector<uint8_t> cpu_img(static_cast<size_t>(kW) * static_cast<size_t>(kH) * 4u);
        for (int y = 0; y < kH; ++y)
        {
            const bool top_half = (y < kH / 2);
            for (int x = 0; x < kW; ++x)
            {
                const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(kW) + static_cast<size_t>(x)) * 4u;
                cpu_img[o + 0] = top_half ? 255 : 0;
                cpu_img[o + 1] = 0;
                cpu_img[o + 2] = top_half ? 0 : 255;
                cpu_img[o + 3] = 255;
            }
        }
        display->PresentRgba8(cpu_img.data(), kW, kH, kW * 4);
        display->PresentRgba8(cpu_img.data(), kW, kH, kW * 4);
        // Let UI thread process the second PresentRgba8
        display->SubmitRenderJob([]() {});
        display->WaitRenderIdle();
        std::vector<uint8_t> flip_fb;
        read_default_fb(flip_fb);
        // GL row 0 is bottom (blue), GL row kH*2 - 10 is top (red)
        const size_t bot_idx = (10u * static_cast<size_t>(kW * 2) + 100u) * 4u;
        const size_t top_idx = ((static_cast<size_t>(kH * 2) - 10u) * static_cast<size_t>(kW * 2) + 100u) * 4u;
        TEST_ASSERT(flip_fb[top_idx + 0] >= 120 && flip_fb[top_idx + 2] <= 20);
        TEST_ASSERT(flip_fb[bot_idx + 0] <= 20 && flip_fb[bot_idx + 2] >= 120);

        TEST_ASSERT(repsp::gpu::SelectRenderBackend("software") == 0);
        repsp::platform::Shutdown();
        std::filesystem::remove_all(shader_dir, ec);
    }

    std::printf("gl_probe: PASS\n");
    return 0;
}
