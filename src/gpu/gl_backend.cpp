#include "repsp/gpu/render_backend.h"
#include "repsp/platform/platform.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "gl_functions.h"
#include "texture_decode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace repsp::gpu
{

namespace
{

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 272;
constexpr size_t kMaxTexCacheEntries = 512;

struct GlVertex
{
    float x;
    float y;
    float z;
    float u;
    float v;
    uint32_t rgba;
};

struct TextureKey
{
    uint32_t addr = 0;
    int width = 0;
    int height = 0;
    int bw = 0;
    int fmt = 0;
    int swizzle = 0;
    int clut_fmt = 0;
    int clut_start = 0;
    int clut_shift = 0;
    int clut_mask = 0;

    bool operator==(const TextureKey& o) const
    {
        return addr == o.addr && width == o.width && height == o.height && bw == o.bw &&
               fmt == o.fmt && swizzle == o.swizzle && clut_fmt == o.clut_fmt &&
               clut_start == o.clut_start && clut_shift == o.clut_shift &&
               clut_mask == o.clut_mask;
    }
};

struct TextureKeyHash
{
    size_t operator()(const TextureKey& k) const
    {
        uint64_t h = 14695981039346656037ull;
        auto mix = [&h](uint64_t v)
        {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(k.addr);
        mix(static_cast<uint64_t>(k.width));
        mix(static_cast<uint64_t>(k.height));
        mix(static_cast<uint64_t>(k.bw));
        mix(static_cast<uint64_t>(k.fmt));
        mix(static_cast<uint64_t>(k.swizzle));
        mix(static_cast<uint64_t>(k.clut_fmt));
        mix(static_cast<uint64_t>(k.clut_start));
        mix(static_cast<uint64_t>(k.clut_shift));
        mix(static_cast<uint64_t>(k.clut_mask));
        return static_cast<size_t>(h);
    }
};

struct PipelineState
{
    uint32_t target_addr = 0;
    int gl_prim = 0;

    int clear = 0;
    int clear_color = 1;
    int clear_alpha = 1;
    int clear_depth = 1;

    int tex_enable = 0;
    uint64_t tex_cache_id = 0;
    uint32_t fbo_tex_addr = 0;
    int tex_w = 0;
    int tex_h = 0;
    int tex_clamp_u = 0;
    int tex_clamp_v = 0;
    int tex_func = 0;
    int tex_alpha = 1;
    int tex_double = 0;
    uint32_t tex_env = 0;

    int ctest = 0;
    int ctest_func = 1;
    uint32_t ctest_ref = 0;
    uint32_t ctest_mask = 0xFFFFFFu;

    int atest = 0;
    int atest_func = 1;
    int atest_ref = 0;
    int atest_mask = 0xFF;

    int sc_x0 = 0;
    int sc_y0 = 0;
    int sc_x1 = 479;
    int sc_y1 = 271;

    int ztest = 0;
    int zfunc = 0;
    int zwrite = 1;

    int blend = 0;
    int blend_op = 0;
    int blend_src = 0;
    int blend_dst = 0;
    uint32_t blend_sfix = 0;
    uint32_t blend_dfix = 0;

    bool operator==(const PipelineState& o) const
    {
        return std::memcmp(this, &o, sizeof(PipelineState)) == 0;
    }
};

struct DrawBatch
{
    PipelineState state;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
};

struct TextureUpload
{
    uint64_t cache_id = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct FlushPayload
{
    std::vector<GlVertex> vertices;
    std::vector<DrawBatch> batches;
    std::vector<TextureUpload> uploads;
    std::vector<uint64_t> evicted_cache_ids;
};

struct GuestTargetTrack
{
    uint32_t addr = 0;
    uint32_t stride = 0;
    int fmt = 3;
    bool has_drawn = false;
    uint64_t last_vram_hash = 0;
};

struct GuestTexEntry
{
    uint64_t cache_id = 0;
    uint64_t content_hash = 0;
    uint64_t lru_tick = 0;
};

struct UiFboTarget
{
    GLuint fbo = 0;
    GLuint color_tex = 0;
    GLuint depth_rbo = 0;
    int scale = 1;
};

struct UiTexEntry
{
    GLuint tex = 0;
    int width = 0;
    int height = 0;
};

uint64_t HashBytes(const uint8_t* data, size_t len, uint64_t seed = 14695981039346656037ull)
{
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= static_cast<uint64_t>(data[i]);
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t HashGuestRange(uint32_t addr, uint32_t size)
{
    if (size == 0)
    {
        return 0;
    }
    const auto& mem = runtime::GetGuestMemory();
    const uint8_t* ptr = mem.Translate(addr);
    if (ptr == nullptr)
    {
        return 0;
    }
    if (addr >= runtime::GuestMemory::kVramAddress)
    {
        const uint32_t off = addr - runtime::GuestMemory::kVramAddress;
        if (off > runtime::GuestMemory::kVramSize || size > runtime::GuestMemory::kVramSize - off)
        {
            return 0;
        }
    }
    return HashBytes(ptr, size);
}

uint32_t FbSizeBytes(uint32_t stride, int fmt)
{
    const uint32_t bpp = (fmt == 3) ? 4u : 2u;
    return stride * static_cast<uint32_t>(kScreenHeight) * bpp;
}

struct GlContextState
{
    std::mutex mutex;
    bool initialized = false;
    int internal_scale = 1;

    // Guest-thread recording state
    uint32_t current_target_addr = 0;
    uint32_t current_target_stride = 512;
    int current_target_fmt = 3;
    std::unordered_map<uint32_t, GuestTargetTrack> guest_targets;

    uint64_t next_tex_cache_id = 1;
    uint64_t tex_lru_tick = 1;
    std::unordered_map<TextureKey, GuestTexEntry, TextureKeyHash> guest_tex_cache;

    std::unique_ptr<FlushPayload> pending_flush;

    // UI-thread GL resources (also read-protected for PresentGlTarget metadata)
    GlProcs gl;
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint readback_fbo = 0;
    GLuint readback_tex = 0;
    int readback_w = 0;
    int readback_h = 0;

    GLint u_clear = -1;
    GLint u_tex_enable = -1;
    GLint u_tex = -1;
    GLint u_tex_size = -1;
    GLint u_tex_clamp_u = -1;
    GLint u_tex_clamp_v = -1;
    GLint u_tex_func = -1;
    GLint u_tex_alpha = -1;
    GLint u_tex_double = -1;
    GLint u_tex_env = -1;
    GLint u_tex_is_fbo = -1;
    GLint u_ctest = -1;
    GLint u_ctest_func = -1;
    GLint u_ctest_ref = -1;
    GLint u_ctest_mask = -1;
    GLint u_atest = -1;
    GLint u_atest_func = -1;
    GLint u_atest_ref = -1;
    GLint u_atest_mask = -1;

    std::unordered_map<uint32_t, UiFboTarget> ui_targets;
    std::unordered_map<uint64_t, UiTexEntry> ui_textures;

    // Diagnostics & stats
    OpenGlStats stats;
    bool warned_blend_double_alpha = false;
    bool warned_blend_fix_conflict = false;
    bool warned_blend_abs_diff = false;
    bool warned_unsupported_prim = false;
};

GlContextState g_gl;

GLuint CompileShader(const GlProcs& gl, GLenum type, const char* src)
{
    GLuint shader = gl.glCreateShader(type);
    if (shader == 0)
    {
        return 0;
    }
    gl.glShaderSource(shader, 1, &src, nullptr);
    gl.glCompileShader(shader);
    GLint ok = 0;
    gl.glGetShaderiv(shader, kGlCompileStatus, &ok);
    if (!ok)
    {
        char log_buf[512] = {};
        GLsizei len = 0;
        gl.glGetShaderInfoLog(shader, sizeof(log_buf), &len, log_buf);
        runtime::Log(runtime::LogLevel::kError, "[GL] Shader compile failed: %s", log_buf);
        gl.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool UiInitResources()
{
    auto* display = platform::GetDisplay();
    if (display == nullptr || !display->HasGlContext())
    {
        return false;
    }
    if (!g_gl.gl.LoadAll(*display))
    {
        runtime::Log(runtime::LogLevel::kError, "[GL] Failed to load required OpenGL 3.3 Core functions");
        return false;
    }

    const char* vs_src =
        "#version 330 core\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec2 aUv;\n"
        "layout(location = 2) in vec4 aColor;\n"
        "out vec2 vUv;\n"
        "out vec4 vColor;\n"
        "void main() {\n"
        "    float nx = (aPos.x / 480.0) * 2.0 - 1.0;\n"
        "    float ny = 1.0 - (aPos.y / 272.0) * 2.0;\n"
        "    float nz = clamp(aPos.z / 65535.0, 0.0, 1.0) * 2.0 - 1.0;\n"
        "    gl_Position = vec4(nx, ny, nz, 1.0);\n"
        "    vUv = aUv;\n"
        "    vColor = aColor;\n"
        "}\n";

    const char* fs_src =
        "#version 330 core\n"
        "in vec2 vUv;\n"
        "in vec4 vColor;\n"
        "out vec4 FragColor;\n"
        "uniform int uClear;\n"
        "uniform int uTexEnable;\n"
        "uniform sampler2D uTex;\n"
        "uniform ivec2 uTexSize;\n"
        "uniform int uTexClampU;\n"
        "uniform int uTexClampV;\n"
        "uniform int uTexFunc;\n"
        "uniform int uTexAlpha;\n"
        "uniform int uTexDouble;\n"
        "uniform vec4 uTexEnv;\n"
        "uniform int uTexIsFbo;\n"
        "uniform int uCtest;\n"
        "uniform int uCtestFunc;\n"
        "uniform uvec3 uCtestRef;\n"
        "uniform uvec3 uCtestMask;\n"
        "uniform int uAtest;\n"
        "uniform int uAtestFunc;\n"
        "uniform int uAtestRef;\n"
        "uniform int uAtestMask;\n"
        "int wrapCoord(int c, int sz, int cl) {\n"
        "    if (cl != 0) {\n"
        "        return clamp(c, 0, sz - 1);\n"
        "    }\n"
        "    if ((sz & (sz - 1)) == 0) {\n"
        "        return c & (sz - 1);\n"
        "    }\n"
        "    int m = c % sz;\n"
        "    return (m < 0) ? (m + sz) : m;\n"
        "}\n"
        "void main() {\n"
        "    vec4 c = vColor;\n"
        "    if (uClear != 0) {\n"
        "        FragColor = c;\n"
        "        return;\n"
        "    }\n"
        "    if (uTexEnable != 0) {\n"
        "        int tx = wrapCoord(int(floor(vUv.x)), uTexSize.x, uTexClampU);\n"
        "        int ty = wrapCoord(int(floor(vUv.y)), uTexSize.y, uTexClampV);\n"
        "        if (uTexIsFbo != 0) {\n"
        "            ty = uTexSize.y - 1 - ty;\n"
        "        }\n"
        "        vec4 t = texelFetch(uTex, ivec2(tx, ty), 0);\n"
        "        if (uTexFunc == 0) {\n"
        "            c.rgb = t.rgb * c.rgb;\n"
        "            c.a = (uTexAlpha != 0) ? (t.a * c.a) : c.a;\n"
        "        } else if (uTexFunc == 1) {\n"
        "            c.rgb = (uTexAlpha != 0) ? mix(c.rgb, t.rgb, t.a) : t.rgb;\n"
        "        } else if (uTexFunc == 2) {\n"
        "            c.rgb = mix(c.rgb, uTexEnv.rgb, t.rgb);\n"
        "            c.a = (uTexAlpha != 0) ? (t.a * c.a) : c.a;\n"
        "        } else if (uTexFunc == 3) {\n"
        "            c.rgb = t.rgb;\n"
        "            c.a = (uTexAlpha != 0) ? t.a : c.a;\n"
        "        } else {\n"
        "            c.rgb = min(t.rgb + c.rgb, vec3(1.0));\n"
        "            c.a = (uTexAlpha != 0) ? (t.a * c.a) : c.a;\n"
        "        }\n"
        "        if (uTexDouble != 0) {\n"
        "            c.rgb = min(c.rgb * 2.0, vec3(1.0));\n"
        "        }\n"
        "    }\n"
        "    if (uCtest != 0) {\n"
        "        uvec3 rgb8 = uvec3(round(clamp(c.rgb, 0.0, 1.0) * 255.0));\n"
        "        uvec3 mc = rgb8 & uCtestMask;\n"
        "        uvec3 mr = uCtestRef & uCtestMask;\n"
        "        bool pass = true;\n"
        "        int cf = uCtestFunc & 3;\n"
        "        if (cf == 0) pass = false;\n"
        "        else if (cf == 1) pass = true;\n"
        "        else if (cf == 2) pass = all(equal(mc, mr));\n"
        "        else pass = any(notEqual(mc, mr));\n"
        "        if (!pass) discard;\n"
        "    }\n"
        "    if (uAtest != 0) {\n"
        "        int a8 = int(round(clamp(c.a, 0.0, 1.0) * 255.0)) & uAtestMask;\n"
        "        int ref8 = uAtestRef & uAtestMask;\n"
        "        bool pass = true;\n"
        "        int af = uAtestFunc & 7;\n"
        "        if (af == 0) pass = false;\n"
        "        else if (af == 1) pass = true;\n"
        "        else if (af == 2) pass = (a8 == ref8);\n"
        "        else if (af == 3) pass = (a8 != ref8);\n"
        "        else if (af == 4) pass = (a8 < ref8);\n"
        "        else if (af == 5) pass = (a8 <= ref8);\n"
        "        else if (af == 6) pass = (a8 > ref8);\n"
        "        else pass = (a8 >= ref8);\n"
        "        if (!pass) discard;\n"
        "    }\n"
        "    FragColor = c;\n"
        "}\n";

    GLuint vs = CompileShader(g_gl.gl, kGlVertexShader, vs_src);
    GLuint fs = CompileShader(g_gl.gl, kGlFragmentShader, fs_src);
    if (vs == 0 || fs == 0)
    {
        if (vs != 0) g_gl.gl.glDeleteShader(vs);
        if (fs != 0) g_gl.gl.glDeleteShader(fs);
        return false;
    }

    g_gl.program = g_gl.gl.glCreateProgram();
    g_gl.gl.glAttachShader(g_gl.program, vs);
    g_gl.gl.glAttachShader(g_gl.program, fs);
    g_gl.gl.glLinkProgram(g_gl.program);
    g_gl.gl.glDeleteShader(vs);
    g_gl.gl.glDeleteShader(fs);

    GLint linked = 0;
    g_gl.gl.glGetProgramiv(g_gl.program, kGlLinkStatus, &linked);
    if (!linked)
    {
        char log_buf[512] = {};
        GLsizei len = 0;
        g_gl.gl.glGetProgramInfoLog(g_gl.program, sizeof(log_buf), &len, log_buf);
        runtime::Log(runtime::LogLevel::kError, "[GL] Program link failed: %s", log_buf);
        g_gl.gl.glDeleteProgram(g_gl.program);
        g_gl.program = 0;
        return false;
    }

    g_gl.u_clear = g_gl.gl.glGetUniformLocation(g_gl.program, "uClear");
    g_gl.u_tex_enable = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexEnable");
    g_gl.u_tex = g_gl.gl.glGetUniformLocation(g_gl.program, "uTex");
    g_gl.u_tex_size = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexSize");
    g_gl.u_tex_clamp_u = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexClampU");
    g_gl.u_tex_clamp_v = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexClampV");
    g_gl.u_tex_func = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexFunc");
    g_gl.u_tex_alpha = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexAlpha");
    g_gl.u_tex_double = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexDouble");
    g_gl.u_tex_env = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexEnv");
    g_gl.u_tex_is_fbo = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexIsFbo");
    g_gl.u_ctest = g_gl.gl.glGetUniformLocation(g_gl.program, "uCtest");
    g_gl.u_ctest_func = g_gl.gl.glGetUniformLocation(g_gl.program, "uCtestFunc");
    g_gl.u_ctest_ref = g_gl.gl.glGetUniformLocation(g_gl.program, "uCtestRef");
    g_gl.u_ctest_mask = g_gl.gl.glGetUniformLocation(g_gl.program, "uCtestMask");
    g_gl.u_atest = g_gl.gl.glGetUniformLocation(g_gl.program, "uAtest");
    g_gl.u_atest_func = g_gl.gl.glGetUniformLocation(g_gl.program, "uAtestFunc");
    g_gl.u_atest_ref = g_gl.gl.glGetUniformLocation(g_gl.program, "uAtestRef");
    g_gl.u_atest_mask = g_gl.gl.glGetUniformLocation(g_gl.program, "uAtestMask");

    g_gl.gl.glGenVertexArrays(1, &g_gl.vao);
    g_gl.gl.glGenBuffers(1, &g_gl.vbo);
    g_gl.gl.glBindVertexArray(g_gl.vao);
    g_gl.gl.glBindBuffer(kGlArrayBuffer, g_gl.vbo);
    g_gl.gl.glEnableVertexAttribArray(0);
    g_gl.gl.glVertexAttribPointer(0, 3, kGlFloat, kGlFalse, sizeof(GlVertex),
                                  reinterpret_cast<const void*>(offsetof(GlVertex, x)));
    g_gl.gl.glEnableVertexAttribArray(1);
    g_gl.gl.glVertexAttribPointer(1, 2, kGlFloat, kGlFalse, sizeof(GlVertex),
                                  reinterpret_cast<const void*>(offsetof(GlVertex, u)));
    g_gl.gl.glEnableVertexAttribArray(2);
    g_gl.gl.glVertexAttribPointer(2, 4, kGlUnsignedByte, kGlTrue, sizeof(GlVertex),
                                  reinterpret_cast<const void*>(offsetof(GlVertex, rgba)));
    g_gl.gl.glBindVertexArray(0);
    g_gl.gl.glBindBuffer(kGlArrayBuffer, 0);
    return g_gl.vao != 0 && g_gl.vbo != 0;
}

void UiDestroyTarget(UiFboTarget& t)
{
    if (t.fbo != 0)
    {
        g_gl.gl.glDeleteFramebuffers(1, &t.fbo);
        t.fbo = 0;
    }
    if (t.color_tex != 0)
    {
        g_gl.gl.glDeleteTextures(1, &t.color_tex);
        t.color_tex = 0;
    }
    if (t.depth_rbo != 0)
    {
        g_gl.gl.glDeleteRenderbuffers(1, &t.depth_rbo);
        t.depth_rbo = 0;
    }
}

void UiShutdownResources()
{
    if (g_gl.gl.glDeleteFramebuffers == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    for (auto& kv : g_gl.ui_targets)
    {
        UiDestroyTarget(kv.second);
    }
    g_gl.ui_targets.clear();

    for (auto& kv : g_gl.ui_textures)
    {
        if (kv.second.tex != 0)
        {
            g_gl.gl.glDeleteTextures(1, &kv.second.tex);
        }
    }
    g_gl.ui_textures.clear();

    if (g_gl.readback_fbo != 0)
    {
        g_gl.gl.glDeleteFramebuffers(1, &g_gl.readback_fbo);
        g_gl.readback_fbo = 0;
    }
    if (g_gl.readback_tex != 0)
    {
        g_gl.gl.glDeleteTextures(1, &g_gl.readback_tex);
        g_gl.readback_tex = 0;
    }
    g_gl.readback_w = 0;
    g_gl.readback_h = 0;

    if (g_gl.vbo != 0)
    {
        g_gl.gl.glDeleteBuffers(1, &g_gl.vbo);
        g_gl.vbo = 0;
    }
    if (g_gl.vao != 0)
    {
        g_gl.gl.glDeleteVertexArrays(1, &g_gl.vao);
        g_gl.vao = 0;
    }
    if (g_gl.program != 0)
    {
        g_gl.gl.glDeleteProgram(g_gl.program);
        g_gl.program = 0;
    }
}

UiFboTarget& UiGetOrCreateTarget(uint32_t addr, int scale)
{
    auto& t = g_gl.ui_targets[addr];
    if (t.fbo != 0 && t.scale == scale)
    {
        return t;
    }
    UiDestroyTarget(t);

    const int w = kScreenWidth * scale;
    const int h = kScreenHeight * scale;
    g_gl.gl.glGenFramebuffers(1, &t.fbo);
    g_gl.gl.glGenTextures(1, &t.color_tex);
    g_gl.gl.glGenRenderbuffers(1, &t.depth_rbo);

    g_gl.gl.glBindTexture(kGlTexture2d, t.color_tex);
    g_gl.gl.glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgba8), w, h, 0,
                         kGlRgba, kGlUnsignedByte, nullptr);
    g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureMinFilter, kGlNearest);
    g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureMagFilter, kGlNearest);
    g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureWrapS, kGlClampToEdge);
    g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureWrapT, kGlClampToEdge);

    g_gl.gl.glBindRenderbuffer(kGlRenderbuffer, t.depth_rbo);
    g_gl.gl.glRenderbufferStorage(kGlRenderbuffer, kGlDepth24Stencil8, w, h);

    g_gl.gl.glBindFramebuffer(kGlFramebuffer, t.fbo);
    g_gl.gl.glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2d, t.color_tex, 0);
    g_gl.gl.glFramebufferRenderbuffer(kGlFramebuffer, kGlDepthStencilAttachment, kGlRenderbuffer, t.depth_rbo);
    t.scale = scale;

    g_gl.gl.glDisable(kGlScissorTest);
    g_gl.gl.glColorMask(kGlTrue, kGlTrue, kGlTrue, kGlTrue);
    g_gl.gl.glDepthMask(kGlTrue);
    g_gl.gl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    g_gl.gl.glClear(kGlColorBufferBit | kGlDepthBufferBit | kGlStencilBufferBit);
    g_gl.gl.glBindFramebuffer(kGlFramebuffer, 0);
    return t;
}

GLenum MapDepthFunc(int zfunc)
{
    static constexpr GLenum kFuncs[8] = {
        kGlNever, kGlAlways, kGlEqual, kGlNotequal,
        kGlLess, kGlLequal, kGlGreater, kGlGequal
    };
    return kFuncs[zfunc & 7];
}

GLenum MapBlendFactor(int code, bool is_src, uint32_t fix, bool& out_approx_double,
                      bool& out_uses_const, GLfloat out_const[3])
{
    switch (code)
    {
    case 0: return is_src ? kGlDstColor : kGlSrcColor;
    case 1: return is_src ? kGlOneMinusDstColor : kGlOneMinusSrcColor;
    case 2: return kGlSrcAlpha;
    case 3: return kGlOneMinusSrcAlpha;
    case 4: return kGlDstAlpha;
    case 5: return kGlOneMinusDstAlpha;
    case 6:
        out_approx_double = true;
        return kGlSrcAlpha;
    case 7:
        out_approx_double = true;
        return kGlOneMinusSrcAlpha;
    case 8:
        out_approx_double = true;
        return kGlDstAlpha;
    case 9:
        out_approx_double = true;
        return kGlOneMinusDstAlpha;
    default:
        if ((fix & 0xFFFFFFu) == 0u)
        {
            return kGlZero;
        }
        if ((fix & 0xFFFFFFu) == 0xFFFFFFu)
        {
            return kGlOne;
        }
        out_uses_const = true;
        out_const[0] = static_cast<float>(fix & 0xFFu) / 255.0f;
        out_const[1] = static_cast<float>((fix >> 8) & 0xFFu) / 255.0f;
        out_const[2] = static_cast<float>((fix >> 16) & 0xFFu) / 255.0f;
        return kGlConstantColor;
    }
}

void ApplyBlendState(const PipelineState& s)
{
    if (!s.blend || s.clear)
    {
        g_gl.gl.glDisable(kGlBlend);
        return;
    }
    g_gl.gl.glEnable(kGlBlend);

    bool approx_double = false;
    bool src_const = false;
    bool dst_const = false;
    GLfloat src_col[3] = {0.0f, 0.0f, 0.0f};
    GLfloat dst_col[3] = {0.0f, 0.0f, 0.0f};

    GLenum src_rgb = MapBlendFactor(s.blend_src, true, s.blend_sfix, approx_double, src_const, src_col);
    GLenum dst_rgb = MapBlendFactor(s.blend_dst, false, s.blend_dfix, approx_double, dst_const, dst_col);

    if (src_const && dst_const)
    {
        const uint32_t sf = s.blend_sfix & 0xFFFFFFu;
        const uint32_t df = s.blend_dfix & 0xFFFFFFu;
        if (df == ((~sf) & 0xFFFFFFu))
        {
            dst_rgb = kGlOneMinusConstantColor;
        }
        else if (sf != df)
        {
            ++g_gl.stats.approximated_blends;
            if (!g_gl.warned_blend_fix_conflict)
            {
                g_gl.warned_blend_fix_conflict = true;
                runtime::Log(runtime::LogLevel::kWarn,
                             "[GL] Blend src/dst fix colors differ (sfix=0x%06x dfix=0x%06x); using sfix",
                             sf, df);
            }
        }
    }

    if (src_const)
    {
        g_gl.gl.glBlendColor(src_col[0], src_col[1], src_col[2], 1.0f);
    }
    else if (dst_const)
    {
        g_gl.gl.glBlendColor(dst_col[0], dst_col[1], dst_col[2], 1.0f);
    }

    if (approx_double)
    {
        ++g_gl.stats.approximated_blends;
        if (!g_gl.warned_blend_double_alpha)
        {
            g_gl.warned_blend_double_alpha = true;
            runtime::Log(runtime::LogLevel::kWarn,
                         "[GL] Approximating 2x alpha blend factor (src=%d dst=%d)",
                         s.blend_src, s.blend_dst);
        }
    }

    GLenum eq_rgb = kGlFuncAdd;
    switch (s.blend_op)
    {
    case 0: eq_rgb = kGlFuncAdd; break;
    case 1: eq_rgb = kGlFuncSubtract; break;
    case 2: eq_rgb = kGlFuncReverseSubtract; break;
    case 3: eq_rgb = kGlMin; break;
    case 4: eq_rgb = kGlMax; break;
    default:
        eq_rgb = kGlFuncSubtract;
        ++g_gl.stats.approximated_blends;
        if (!g_gl.warned_blend_abs_diff)
        {
            g_gl.warned_blend_abs_diff = true;
            runtime::Log(runtime::LogLevel::kWarn,
                         "[GL] Approximating abs-diff blend op (5) with subtract");
        }
        break;
    }

    g_gl.gl.glBlendEquationSeparate(eq_rgb, kGlFuncAdd);
    g_gl.gl.glBlendFuncSeparate(src_rgb, dst_rgb, kGlOne, kGlZero);
}

void UiExecuteFlush(std::unique_ptr<FlushPayload> payload, int scale)
{
    if (!payload)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_gl.mutex);

    for (uint64_t evicted_id : payload->evicted_cache_ids)
    {
        auto it = g_gl.ui_textures.find(evicted_id);
        if (it != g_gl.ui_textures.end())
        {
            if (it->second.tex != 0)
            {
                g_gl.gl.glDeleteTextures(1, &it->second.tex);
            }
            g_gl.ui_textures.erase(it);
        }
    }

    for (const auto& up : payload->uploads)
    {
        auto& entry = g_gl.ui_textures[up.cache_id];
        if (entry.tex == 0)
        {
            g_gl.gl.glGenTextures(1, &entry.tex);
        }
        g_gl.gl.glActiveTexture(kGlTexture0);
        g_gl.gl.glBindTexture(kGlTexture2d, entry.tex);
        g_gl.gl.glPixelStorei(kGlUnpackAlignment, 4);
        g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureMinFilter, kGlNearest);
        g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureMagFilter, kGlNearest);
        if (entry.width != up.width || entry.height != up.height)
        {
            g_gl.gl.glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgba8),
                                 up.width, up.height, 0, kGlRgba, kGlUnsignedByte, up.rgba.data());
            entry.width = up.width;
            entry.height = up.height;
        }
        else
        {
            g_gl.gl.glTexSubImage2D(kGlTexture2d, 0, 0, 0, up.width, up.height,
                                    kGlRgba, kGlUnsignedByte, up.rgba.data());
        }
        ++g_gl.stats.texture_uploads;
    }

    if (payload->batches.empty() || payload->vertices.empty())
    {
        return;
    }

    g_gl.gl.glUseProgram(g_gl.program);
    g_gl.gl.glBindVertexArray(g_gl.vao);
    g_gl.gl.glBindBuffer(kGlArrayBuffer, g_gl.vbo);
    g_gl.gl.glBufferData(kGlArrayBuffer,
                         static_cast<GLsizeiptr>(payload->vertices.size() * sizeof(GlVertex)),
                         payload->vertices.data(), kGlStreamDraw);

    g_gl.gl.glDisable(kGlCullFace);
    g_gl.gl.glDisable(kGlStencilTest);

    uint32_t bound_target = 0xFFFFFFFFu;
    for (const auto& batch : payload->batches)
    {
        const PipelineState& s = batch.state;
        if (s.target_addr == 0)
        {
            continue;
        }
        if (s.target_addr != bound_target)
        {
            UiFboTarget& fbo = UiGetOrCreateTarget(s.target_addr, scale);
            g_gl.gl.glBindFramebuffer(kGlFramebuffer, fbo.fbo);
            g_gl.gl.glViewport(0, 0, kScreenWidth * scale, kScreenHeight * scale);
            bound_target = s.target_addr;
            ++g_gl.stats.fbo_switches;
        }

        const int x0 = std::clamp(s.sc_x0, 0, kScreenWidth - 1);
        const int y0 = std::clamp(s.sc_y0, 0, kScreenHeight - 1);
        const int x1 = std::clamp(s.sc_x1, 0, kScreenWidth - 1);
        const int y1 = std::clamp(s.sc_y1, 0, kScreenHeight - 1);
        if (x1 < x0 || y1 < y0)
        {
            continue;
        }
        g_gl.gl.glEnable(kGlScissorTest);
        g_gl.gl.glScissor(x0 * scale, (kScreenHeight - 1 - y1) * scale,
                          (x1 - x0 + 1) * scale, (y1 - y0 + 1) * scale);

        if (s.clear)
        {
            if (!s.clear_color && !s.clear_alpha && !s.clear_depth)
            {
                continue;
            }
            g_gl.gl.glColorMask(s.clear_color ? kGlTrue : kGlFalse,
                                s.clear_color ? kGlTrue : kGlFalse,
                                s.clear_color ? kGlTrue : kGlFalse,
                                s.clear_alpha ? kGlTrue : kGlFalse);
            if (s.clear_depth)
            {
                g_gl.gl.glEnable(kGlDepthTest);
                g_gl.gl.glDepthFunc(kGlAlways);
                g_gl.gl.glDepthMask(kGlTrue);
            }
            else
            {
                g_gl.gl.glDisable(kGlDepthTest);
                g_gl.gl.glDepthMask(kGlFalse);
            }
        }
        else
        {
            g_gl.gl.glColorMask(kGlTrue, kGlTrue, kGlTrue, kGlTrue);
            if (s.ztest)
            {
                g_gl.gl.glEnable(kGlDepthTest);
                g_gl.gl.glDepthFunc(MapDepthFunc(s.zfunc));
                g_gl.gl.glDepthMask(s.zwrite ? kGlTrue : kGlFalse);
            }
            else
            {
                g_gl.gl.glDisable(kGlDepthTest);
                g_gl.gl.glDepthMask(kGlFalse);
            }
        }

        ApplyBlendState(s);

        g_gl.gl.glUniform1i(g_gl.u_clear, s.clear ? 1 : 0);
        if (!s.clear && s.tex_enable)
        {
            GLuint tex_id = 0;
            int tex_w = s.tex_w;
            int tex_h = s.tex_h;
            int is_fbo = 0;
            if (s.fbo_tex_addr != 0)
            {
                auto fit = g_gl.ui_targets.find(s.fbo_tex_addr);
                if (fit != g_gl.ui_targets.end() && fit->second.color_tex != 0)
                {
                    tex_id = fit->second.color_tex;
                    is_fbo = 1;
                }
            }
            if (tex_id == 0 && s.tex_cache_id != 0)
            {
                auto tit = g_gl.ui_textures.find(s.tex_cache_id);
                if (tit != g_gl.ui_textures.end())
                {
                    tex_id = tit->second.tex;
                }
            }
            g_gl.gl.glActiveTexture(kGlTexture0);
            g_gl.gl.glBindTexture(kGlTexture2d, tex_id);
            g_gl.gl.glUniform1i(g_gl.u_tex_enable, (tex_id != 0) ? 1 : 0);
            g_gl.gl.glUniform1i(g_gl.u_tex, 0);
            g_gl.gl.glUniform2i(g_gl.u_tex_size, std::max(1, tex_w), std::max(1, tex_h));
            g_gl.gl.glUniform1i(g_gl.u_tex_clamp_u, s.tex_clamp_u);
            g_gl.gl.glUniform1i(g_gl.u_tex_clamp_v, s.tex_clamp_v);
            g_gl.gl.glUniform1i(g_gl.u_tex_func, s.tex_func);
            g_gl.gl.glUniform1i(g_gl.u_tex_alpha, s.tex_alpha);
            g_gl.gl.glUniform1i(g_gl.u_tex_double, s.tex_double);
            g_gl.gl.glUniform4f(g_gl.u_tex_env,
                                static_cast<float>(s.tex_env & 0xFFu) / 255.0f,
                                static_cast<float>((s.tex_env >> 8) & 0xFFu) / 255.0f,
                                static_cast<float>((s.tex_env >> 16) & 0xFFu) / 255.0f,
                                static_cast<float>((s.tex_env >> 24) & 0xFFu) / 255.0f);
            g_gl.gl.glUniform1i(g_gl.u_tex_is_fbo, is_fbo);
        }
        else
        {
            g_gl.gl.glUniform1i(g_gl.u_tex_enable, 0);
        }

        g_gl.gl.glUniform1i(g_gl.u_ctest, s.ctest);
        g_gl.gl.glUniform1i(g_gl.u_ctest_func, s.ctest_func);
        g_gl.gl.glUniform3ui(g_gl.u_ctest_ref,
                             s.ctest_ref & 0xFFu,
                             (s.ctest_ref >> 8) & 0xFFu,
                             (s.ctest_ref >> 16) & 0xFFu);
        g_gl.gl.glUniform3ui(g_gl.u_ctest_mask,
                             s.ctest_mask & 0xFFu,
                             (s.ctest_mask >> 8) & 0xFFu,
                             (s.ctest_mask >> 16) & 0xFFu);

        g_gl.gl.glUniform1i(g_gl.u_atest, s.atest);
        g_gl.gl.glUniform1i(g_gl.u_atest_func, s.atest_func);
        g_gl.gl.glUniform1i(g_gl.u_atest_ref, s.atest_ref);
        g_gl.gl.glUniform1i(g_gl.u_atest_mask, s.atest_mask);

        g_gl.gl.glDrawArrays(static_cast<GLenum>(s.gl_prim),
                             static_cast<GLint>(batch.first_vertex),
                             static_cast<GLsizei>(batch.vertex_count));
        ++g_gl.stats.draw_calls;
    }

    g_gl.gl.glDisable(kGlScissorTest);
    g_gl.gl.glDisable(kGlBlend);
    g_gl.gl.glDisable(kGlDepthTest);
    g_gl.gl.glColorMask(kGlTrue, kGlTrue, kGlTrue, kGlTrue);
    g_gl.gl.glDepthMask(kGlTrue);
    g_gl.gl.glBindVertexArray(0);
    g_gl.gl.glBindBuffer(kGlArrayBuffer, 0);
    g_gl.gl.glBindTexture(kGlTexture2d, 0);
    g_gl.gl.glBindFramebuffer(kGlFramebuffer, 0);
    g_gl.gl.glUseProgram(0);
}

void CheckCpuWriteOverlapLocked(GuestTargetTrack& track)
{
    if (!track.has_drawn || track.addr == 0 || track.stride == 0)
    {
        return;
    }
    const uint32_t size = FbSizeBytes(track.stride, track.fmt);
    const uint64_t h = HashGuestRange(track.addr, size);
    if (h != 0 && track.last_vram_hash != 0 && h != track.last_vram_hash)
    {
        ++g_gl.stats.cpu_write_overlaps;
        runtime::Log(runtime::LogLevel::kWarn,
                     "[GL] Diagnostic: guest CPU write overlap detected on FBO target 0x%08x",
                     track.addr);
        track.last_vram_hash = h;
    }
}

void UpdateTargetVramHashLocked(GuestTargetTrack& track)
{
    if (track.addr == 0 || track.stride == 0)
    {
        return;
    }
    const uint32_t size = FbSizeBytes(track.stride, track.fmt);
    track.last_vram_hash = HashGuestRange(track.addr, size);
    track.has_drawn = true;
}

uint32_t FindOverlappingTargetLocked(uint32_t tex_addr, uint32_t tex_size)
{
    if (tex_addr == 0)
    {
        return 0;
    }
    const uint64_t t_end = static_cast<uint64_t>(tex_addr) + std::max<uint32_t>(1u, tex_size);
    for (const auto& kv : g_gl.guest_targets)
    {
        const auto& t = kv.second;
        if (!t.has_drawn || t.addr == 0 || t.stride == 0)
        {
            continue;
        }
        const uint64_t f_start = t.addr;
        const uint64_t f_end = f_start + FbSizeBytes(t.stride, t.fmt);
        if (tex_addr < f_end && t_end > f_start)
        {
            return t.addr;
        }
    }
    return 0;
}

uint64_t ResolveTextureLocked(const DrawState& st, FlushPayload& flush, uint32_t& out_fbo_addr)
{
    out_fbo_addr = 0;
    if (!st.tex_enable || st.tex_w <= 0 || st.tex_h <= 0 || st.tex_bw <= 0 || st.tex_fmt > kTpsmDxt5)
    {
        return 0;
    }

    const uint32_t data_bytes = ComputeTextureDataSizeBytes(st);
    out_fbo_addr = FindOverlappingTargetLocked(st.tex_addr, data_bytes);
    if (out_fbo_addr != 0)
    {
        return 0;
    }

    uint64_t content_hash = HashGuestRange(st.tex_addr, data_bytes);
    if (st.tex_fmt >= kTpsmT4 && st.tex_fmt <= kTpsmT32 && st.clut != nullptr)
    {
        const size_t clut_bytes = (st.clut_fmt == 3) ? (256u * 4u) : (512u * 2u);
        content_hash = HashBytes(st.clut, clut_bytes, content_hash);
    }

    TextureKey key;
    key.addr = st.tex_addr;
    key.width = st.tex_w;
    key.height = st.tex_h;
    key.bw = st.tex_bw;
    key.fmt = st.tex_fmt;
    key.swizzle = st.tex_swizzle;
    key.clut_fmt = st.clut_fmt;
    key.clut_start = st.clut_start;
    key.clut_shift = st.clut_shift;
    key.clut_mask = st.clut_mask;

    auto it = g_gl.guest_tex_cache.find(key);
    if (it != g_gl.guest_tex_cache.end() && it->second.content_hash == content_hash)
    {
        it->second.lru_tick = ++g_gl.tex_lru_tick;
        ++g_gl.stats.texture_cache_hits;
        return it->second.cache_id;
    }

    uint64_t cache_id = 0;
    if (it != g_gl.guest_tex_cache.end())
    {
        cache_id = it->second.cache_id;
        it->second.content_hash = content_hash;
        it->second.lru_tick = ++g_gl.tex_lru_tick;
    }
    else
    {
        if (g_gl.guest_tex_cache.size() >= kMaxTexCacheEntries)
        {
            auto oldest = g_gl.guest_tex_cache.begin();
            for (auto cur = g_gl.guest_tex_cache.begin(); cur != g_gl.guest_tex_cache.end(); ++cur)
            {
                if (cur->second.lru_tick < oldest->second.lru_tick)
                {
                    oldest = cur;
                }
            }
            flush.evicted_cache_ids.push_back(oldest->second.cache_id);
            g_gl.guest_tex_cache.erase(oldest);
        }
        cache_id = g_gl.next_tex_cache_id++;
        g_gl.guest_tex_cache.emplace(key, GuestTexEntry{cache_id, content_hash, ++g_gl.tex_lru_tick});
    }

    TextureUpload up;
    up.cache_id = cache_id;
    up.width = st.tex_w;
    up.height = st.tex_h;
    up.rgba.resize(static_cast<size_t>(st.tex_w) * static_cast<size_t>(st.tex_h) * 4u);
    DecodeTextureRgba8(st, up.rgba.data(), nullptr);
    flush.uploads.push_back(std::move(up));
    return cache_id;
}

void FlushPendingLocked()
{
    auto* display = platform::GetDisplay();
    if (display == nullptr || !display->HasGlContext())
    {
        g_gl.pending_flush.reset();
        return;
    }
    if (!g_gl.pending_flush ||
        (g_gl.pending_flush->batches.empty() && g_gl.pending_flush->uploads.empty() &&
         g_gl.pending_flush->evicted_cache_ids.empty()))
    {
        g_gl.pending_flush.reset();
        return;
    }

    auto shared_payload = std::shared_ptr<FlushPayload>(std::move(g_gl.pending_flush));
    const int scale = g_gl.internal_scale;
    display->SubmitRenderJob([shared_payload, scale]()
    {
        UiExecuteFlush(std::make_unique<FlushPayload>(std::move(*shared_payload)), scale);
    });
}

int GlInit(int width, int height)
{
    (void)width;
    (void)height;
    auto* display = platform::GetDisplay();
    if (display == nullptr || !display->HasGlContext())
    {
        return -1;
    }
    bool ok = false;
    display->SubmitRenderJob([&ok]()
    {
        ok = UiInitResources();
    });
    display->WaitRenderIdle();
    if (!ok)
    {
        return -1;
    }
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    g_gl.initialized = true;
    g_gl.pending_flush.reset();
    return 0;
}

void GlShutdown()
{
    auto* display = platform::GetDisplay();
    {
        std::lock_guard<std::mutex> lock(g_gl.mutex);
        g_gl.pending_flush.reset();
        g_gl.guest_targets.clear();
        g_gl.guest_tex_cache.clear();
        g_gl.initialized = false;
    }
    if (display != nullptr && display->HasGlContext())
    {
        display->SubmitRenderJob([]()
        {
            UiShutdownResources();
        });
        display->WaitRenderIdle();
    }
}

void GlTarget(uint32_t addr, uint32_t stride, int fmt)
{
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    g_gl.current_target_addr = addr;
    g_gl.current_target_stride = stride;
    g_gl.current_target_fmt = fmt;
    if (addr != 0)
    {
        auto& track = g_gl.guest_targets[addr];
        track.addr = addr;
        track.stride = stride;
        track.fmt = fmt;
        CheckCpuWriteOverlapLocked(track);
    }
}

GlVertex ToGlVertex(const Vertex& v, uint32_t override_rgba, bool use_override)
{
    GlVertex gv;
    gv.x = static_cast<float>(v.x);
    gv.y = static_cast<float>(v.y);
    gv.z = v.z;
    gv.u = v.u;
    gv.v = v.v;
    gv.rgba = use_override ? override_rgba : v.rgba;
    return gv;
}

void GlDraw(int prim, const Vertex* v, int count, const DrawState* st)
{
    if (v == nullptr || st == nullptr || count <= 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    if (!g_gl.initialized || g_gl.current_target_addr == 0)
    {
        return;
    }

    auto& track = g_gl.guest_targets[g_gl.current_target_addr];
    track.addr = g_gl.current_target_addr;
    track.stride = g_gl.current_target_stride;
    track.fmt = g_gl.current_target_fmt;
    UpdateTargetVramHashLocked(track);

    if (!g_gl.pending_flush)
    {
        g_gl.pending_flush = std::make_unique<FlushPayload>();
    }

    PipelineState ps;
    ps.target_addr = g_gl.current_target_addr;
    ps.clear = st->clear;
    ps.clear_color = st->clear_color;
    ps.clear_alpha = st->clear_alpha;
    ps.clear_depth = st->clear_depth;

    ps.tex_enable = (!st->clear && st->tex_enable) ? 1 : 0;
    if (ps.tex_enable)
    {
        ps.tex_cache_id = ResolveTextureLocked(*st, *g_gl.pending_flush, ps.fbo_tex_addr);
        ps.tex_w = st->tex_w;
        ps.tex_h = st->tex_h;
        ps.tex_clamp_u = st->tex_clamp_u;
        ps.tex_clamp_v = st->tex_clamp_v;
        ps.tex_func = st->tex_func;
        ps.tex_alpha = st->tex_alpha;
        ps.tex_double = st->tex_double;
        ps.tex_env = st->tex_env;
    }

    ps.ctest = st->ctest;
    ps.ctest_func = st->ctest_func;
    ps.ctest_ref = st->ctest_ref;
    ps.ctest_mask = st->ctest_mask;

    ps.atest = st->atest;
    ps.atest_func = st->atest_func;
    ps.atest_ref = st->atest_ref;
    ps.atest_mask = st->atest_mask;

    ps.sc_x0 = st->sc_x0;
    ps.sc_y0 = st->sc_y0;
    ps.sc_x1 = st->sc_x1;
    ps.sc_y1 = st->sc_y1;

    ps.ztest = st->ztest;
    ps.zfunc = st->zfunc;
    ps.zwrite = st->zwrite;

    ps.blend = st->blend;
    ps.blend_op = st->blend_op;
    ps.blend_src = st->blend_src;
    ps.blend_dst = st->blend_dst;
    ps.blend_sfix = st->blend_sfix;
    ps.blend_dfix = st->blend_dfix;

    const uint32_t first_vertex = static_cast<uint32_t>(g_gl.pending_flush->vertices.size());
    auto& verts = g_gl.pending_flush->vertices;

    switch (prim)
    {
    case kPrimPoints:
        ps.gl_prim = static_cast<int>(kGlPoints);
        for (int i = 0; i < count; ++i)
        {
            verts.push_back(ToGlVertex(v[i], 0, false));
        }
        break;
    case kPrimLines:
        ps.gl_prim = static_cast<int>(kGlLines);
        for (int i = 0; i + 1 < count; i += 2)
        {
            const uint32_t flat_col = v[i + 1].rgba;
            verts.push_back(ToGlVertex(v[i], flat_col, !st->gouraud));
            verts.push_back(ToGlVertex(v[i + 1], flat_col, !st->gouraud));
        }
        break;
    case kPrimLineStrip:
        ps.gl_prim = static_cast<int>(kGlLines);
        for (int i = 0; i + 1 < count; ++i)
        {
            const uint32_t flat_col = v[i + 1].rgba;
            verts.push_back(ToGlVertex(v[i], flat_col, !st->gouraud));
            verts.push_back(ToGlVertex(v[i + 1], flat_col, !st->gouraud));
        }
        break;
    case kPrimTriangles:
        ps.gl_prim = static_cast<int>(kGlTriangles);
        for (int i = 0; i + 2 < count; i += 3)
        {
            const uint32_t flat_col = v[i + 2].rgba;
            verts.push_back(ToGlVertex(v[i], flat_col, !st->gouraud));
            verts.push_back(ToGlVertex(v[i + 1], flat_col, !st->gouraud));
            verts.push_back(ToGlVertex(v[i + 2], flat_col, !st->gouraud));
        }
        break;
    case kPrimTriangleStrip:
        ps.gl_prim = static_cast<int>(kGlTriangles);
        for (int i = 0; i + 2 < count; ++i)
        {
            const uint32_t flat_col = v[i + 2].rgba;
            verts.push_back(ToGlVertex(v[i], flat_col, !st->gouraud));
            verts.push_back(ToGlVertex(v[i + 1], flat_col, !st->gouraud));
            verts.push_back(ToGlVertex(v[i + 2], flat_col, !st->gouraud));
        }
        break;
    case kPrimTriangleFan:
        ps.gl_prim = static_cast<int>(kGlTriangles);
        for (int i = 1; i + 1 < count; ++i)
        {
            const uint32_t flat_col = v[i + 1].rgba;
            verts.push_back(ToGlVertex(v[0], flat_col, !st->gouraud));
            verts.push_back(ToGlVertex(v[i], flat_col, !st->gouraud));
            verts.push_back(ToGlVertex(v[i + 1], flat_col, !st->gouraud));
        }
        break;
    case kPrimSprites:
        ps.gl_prim = static_cast<int>(kGlTriangles);
        for (int i = 0; i + 1 < count; i += 2)
        {
            const Vertex& a = v[i];
            const Vertex& b = v[i + 1];
            if (a.x == b.x || a.y == b.y)
            {
                continue;
            }
            const float x0 = static_cast<float>(std::min(a.x, b.x));
            const float x1 = static_cast<float>(std::max(a.x, b.x));
            const float y0 = static_cast<float>(std::min(a.y, b.y));
            const float y1 = static_cast<float>(std::max(a.y, b.y));
            const float u0 = (a.x <= b.x) ? a.u : b.u;
            const float u1 = (a.x <= b.x) ? b.u : a.u;
            const float v0 = (a.y <= b.y) ? a.v : b.v;
            const float v1 = (a.y <= b.y) ? b.v : a.v;
            const float z = b.z;
            const uint32_t rgba = b.rgba;

            verts.push_back(GlVertex{x0, y0, z, u0, v0, rgba});
            verts.push_back(GlVertex{x1, y0, z, u1, v0, rgba});
            verts.push_back(GlVertex{x1, y1, z, u1, v1, rgba});
            verts.push_back(GlVertex{x0, y0, z, u0, v0, rgba});
            verts.push_back(GlVertex{x1, y1, z, u1, v1, rgba});
            verts.push_back(GlVertex{x0, y1, z, u0, v1, rgba});
        }
        break;
    default:
        ++g_gl.stats.unsupported_ops;
        if (!g_gl.warned_unsupported_prim)
        {
            g_gl.warned_unsupported_prim = true;
            runtime::Log(runtime::LogLevel::kWarn, "[GL] Unsupported primitive type %d", prim);
        }
        return;
    }

    const uint32_t added = static_cast<uint32_t>(verts.size()) - first_vertex;
    if (added == 0)
    {
        return;
    }

    auto& batches = g_gl.pending_flush->batches;
    if (!batches.empty() && batches.back().state == ps)
    {
        batches.back().vertex_count += added;
    }
    else
    {
        batches.push_back(DrawBatch{ps, first_vertex, added});
    }
}

void GlFinish()
{
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    FlushPendingLocked();
}

void GlPresent()
{
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    FlushPendingLocked();
}

} // namespace

const RenderBackend kOpenGlBackend = {
    "opengl",
    GlInit,
    GlShutdown,
    GlTarget,
    GlDraw,
    GlFinish,
    GlPresent,
};

void SetInternalScale(int scale)
{
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    g_gl.internal_scale = std::clamp(scale, 1, 8);
}

int GetInternalScale()
{
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    return g_gl.internal_scale;
}

bool HasGlTargetForAddress(uint32_t addr)
{
    if (addr == 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    auto it = g_gl.guest_targets.find(addr);
    return it != g_gl.guest_targets.end() && it->second.has_drawn;
}

bool PresentGlTarget(uint32_t addr)
{
    auto* display = platform::GetDisplay();
    if (display == nullptr || !display->HasGlContext() || addr == 0)
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_gl.mutex);
        FlushPendingLocked();
        auto git = g_gl.guest_targets.find(addr);
        if (git != g_gl.guest_targets.end())
        {
            CheckCpuWriteOverlapLocked(git->second);
        }
    }
    display->WaitRenderIdle();

    uint32_t tex_id = 0;
    int w = 0;
    int h = 0;
    {
        std::lock_guard<std::mutex> lock(g_gl.mutex);
        auto it = g_gl.ui_targets.find(addr);
        if (it == g_gl.ui_targets.end() || it->second.color_tex == 0)
        {
            return false;
        }
        tex_id = it->second.color_tex;
        w = kScreenWidth * it->second.scale;
        h = kScreenHeight * it->second.scale;
    }
    display->PresentGlTexture(tex_id, w, h);
    return true;
}

bool ReadGlTargetRgba8(uint32_t addr, int output_scale, std::vector<uint8_t>& out_rgba, int& out_w, int& out_h)
{
    auto* display = platform::GetDisplay();
    if (display == nullptr || !display->HasGlContext() || addr == 0)
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_gl.mutex);
        FlushPendingLocked();
    }

    const int clamped_out_scale = std::clamp(output_scale, 1, 8);
    bool ok = false;
    display->SubmitRenderJob([addr, clamped_out_scale, &out_rgba, &out_w, &out_h, &ok]()
    {
        std::lock_guard<std::mutex> lock(g_gl.mutex);
        auto it = g_gl.ui_targets.find(addr);
        if (it == g_gl.ui_targets.end() || it->second.fbo == 0)
        {
            ok = false;
            return;
        }
        const int src_w = kScreenWidth * it->second.scale;
        const int src_h = kScreenHeight * it->second.scale;
        const int dst_w = kScreenWidth * clamped_out_scale;
        const int dst_h = kScreenHeight * clamped_out_scale;

        GLuint read_fbo = it->second.fbo;
        if (src_w != dst_w || src_h != dst_h)
        {
            if (g_gl.readback_fbo == 0)
            {
                g_gl.gl.glGenFramebuffers(1, &g_gl.readback_fbo);
                g_gl.gl.glGenTextures(1, &g_gl.readback_tex);
            }
            if (g_gl.readback_w != dst_w || g_gl.readback_h != dst_h)
            {
                g_gl.gl.glBindTexture(kGlTexture2d, g_gl.readback_tex);
                g_gl.gl.glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgba8),
                                     dst_w, dst_h, 0, kGlRgba, kGlUnsignedByte, nullptr);
                g_gl.gl.glBindFramebuffer(kGlFramebuffer, g_gl.readback_fbo);
                g_gl.gl.glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0,
                                               kGlTexture2d, g_gl.readback_tex, 0);
                g_gl.readback_w = dst_w;
                g_gl.readback_h = dst_h;
            }
            g_gl.gl.glBindFramebuffer(kGlReadFramebuffer, it->second.fbo);
            g_gl.gl.glBindFramebuffer(kGlDrawFramebuffer, g_gl.readback_fbo);
            g_gl.gl.glDisable(kGlScissorTest);
            g_gl.gl.glBlitFramebuffer(0, 0, src_w, src_h, 0, 0, dst_w, dst_h,
                                      kGlColorBufferBit, kGlNearest);
            read_fbo = g_gl.readback_fbo;
        }

        std::vector<uint8_t> raw(static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * 4u);
        g_gl.gl.glBindFramebuffer(kGlReadFramebuffer, read_fbo);
        g_gl.gl.glPixelStorei(kGlPackAlignment, 4);
        g_gl.gl.glReadPixels(0, 0, dst_w, dst_h, kGlRgba, kGlUnsignedByte, raw.data());
        g_gl.gl.glBindFramebuffer(kGlFramebuffer, 0);

        out_w = dst_w;
        out_h = dst_h;
        out_rgba.resize(raw.size());
        const size_t row_bytes = static_cast<size_t>(dst_w) * 4u;
        for (int y = 0; y < dst_h; ++y)
        {
            std::memcpy(out_rgba.data() + static_cast<size_t>(y) * row_bytes,
                        raw.data() + static_cast<size_t>(dst_h - 1 - y) * row_bytes,
                        row_bytes);
        }
        ok = true;
    });
    display->WaitRenderIdle();
    return ok;
}

OpenGlStats GetOpenGlStats()
{
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    return g_gl.stats;
}

void ResetOpenGlStats()
{
    std::lock_guard<std::mutex> lock(g_gl.mutex);
    g_gl.stats = OpenGlStats{};
    g_gl.warned_blend_double_alpha = false;
    g_gl.warned_blend_fix_conflict = false;
    g_gl.warned_blend_abs_diff = false;
    g_gl.warned_unsupported_prim = false;
}

} // namespace repsp::gpu
