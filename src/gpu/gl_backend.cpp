#include "repsp/gpu/render_backend.h"
#include "repsp/platform/platform.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/scheduler.h"
#include "gl_functions.h"
#include "texture_decode.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
    int max_level = 0;
    std::array<uint32_t, DrawState::kMaxTexLevels> level_addr = {};
    std::array<int, DrawState::kMaxTexLevels> level_bw = {};
    std::array<int, DrawState::kMaxTexLevels> level_w = {};
    std::array<int, DrawState::kMaxTexLevels> level_h = {};
    int clut_fmt = 0;
    int clut_start = 0;
    int clut_shift = 0;
    int clut_mask = 0;

    bool operator==(const TextureKey& o) const
    {
        return addr == o.addr && width == o.width && height == o.height && bw == o.bw &&
               fmt == o.fmt && swizzle == o.swizzle && max_level == o.max_level &&
               level_addr == o.level_addr && level_bw == o.level_bw &&
               level_w == o.level_w && level_h == o.level_h &&
               clut_fmt == o.clut_fmt && clut_start == o.clut_start &&
               clut_shift == o.clut_shift && clut_mask == o.clut_mask;
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
        mix(static_cast<uint64_t>(k.max_level));
        for (int i = 1; i <= k.max_level && i < DrawState::kMaxTexLevels; ++i)
        {
            mix(k.level_addr[static_cast<size_t>(i)]);
            mix(static_cast<uint64_t>(k.level_bw[static_cast<size_t>(i)]));
            mix(static_cast<uint64_t>(k.level_w[static_cast<size_t>(i)]));
            mix(static_cast<uint64_t>(k.level_h[static_cast<size_t>(i)]));
        }
        mix(static_cast<uint64_t>(k.clut_fmt));
        mix(static_cast<uint64_t>(k.clut_start));
        mix(static_cast<uint64_t>(k.clut_shift));
        mix(static_cast<uint64_t>(k.clut_mask));
        return static_cast<size_t>(h);
    }
};

struct SamplerKey
{
    GLenum min_filter = kGlNearest;
    GLenum mag_filter = kGlNearest;
    GLenum wrap_s = kGlRepeat;
    GLenum wrap_t = kGlRepeat;
    int aniso_int = 1;

    bool operator==(const SamplerKey& o) const
    {
        return min_filter == o.min_filter && mag_filter == o.mag_filter &&
               wrap_s == o.wrap_s && wrap_t == o.wrap_t && aniso_int == o.aniso_int;
    }
};

struct SamplerKeyHash
{
    size_t operator()(const SamplerKey& k) const
    {
        uint64_t h = 14695981039346656037ull;
        auto mix = [&h](uint64_t v)
        {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(k.min_filter);
        mix(k.mag_filter);
        mix(k.wrap_s);
        mix(k.wrap_t);
        mix(static_cast<uint64_t>(k.aniso_int));
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
    int tex_min_filter = 0;
    int tex_mag_filter = 0;
    int tex_level_mode = 0;
    float tex_lod_bias = 0.0f;
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
        return target_addr == o.target_addr && gl_prim == o.gl_prim &&
               clear == o.clear && clear_color == o.clear_color &&
               clear_alpha == o.clear_alpha && clear_depth == o.clear_depth &&
               tex_enable == o.tex_enable && tex_cache_id == o.tex_cache_id &&
               fbo_tex_addr == o.fbo_tex_addr && tex_w == o.tex_w && tex_h == o.tex_h &&
               tex_clamp_u == o.tex_clamp_u && tex_clamp_v == o.tex_clamp_v &&
               tex_min_filter == o.tex_min_filter && tex_mag_filter == o.tex_mag_filter &&
               tex_level_mode == o.tex_level_mode && tex_lod_bias == o.tex_lod_bias &&
               tex_func == o.tex_func && tex_alpha == o.tex_alpha &&
               tex_double == o.tex_double && tex_env == o.tex_env &&
               ctest == o.ctest && ctest_func == o.ctest_func &&
               ctest_ref == o.ctest_ref && ctest_mask == o.ctest_mask &&
               atest == o.atest && atest_func == o.atest_func &&
               atest_ref == o.atest_ref && atest_mask == o.atest_mask &&
               sc_x0 == o.sc_x0 && sc_y0 == o.sc_y0 && sc_x1 == o.sc_x1 && sc_y1 == o.sc_y1 &&
               ztest == o.ztest && zfunc == o.zfunc && zwrite == o.zwrite &&
               blend == o.blend && blend_op == o.blend_op &&
               blend_src == o.blend_src && blend_dst == o.blend_dst &&
               blend_sfix == o.blend_sfix && blend_dfix == o.blend_dfix;
    }
};

struct DrawBatch
{
    PipelineState state;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
};

struct MipLevelUpload
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct TextureUpload
{
    uint64_t cache_id = 0;
    std::vector<MipLevelUpload> levels;
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
    uint64_t clut_hash = 0;
    uint64_t verified_epoch = 0;
    uint64_t lru_tick = 0;
};

struct UiFboTarget
{
    GLuint fbo = 0;
    GLuint color_tex = 0;
    GLuint depth_rbo = 0;
    GLuint msaa_fbo = 0;
    GLuint msaa_color_rbo = 0;
    GLuint msaa_depth_rbo = 0;
    int scale = 1;
    int samples = 1;
    bool msaa_dirty = false;
};

struct UiTexEntry
{
    GLuint tex = 0;
    int width = 0;
    int height = 0;
    int supplied_max_level = 0;
    int uploaded_max_level = 0;
    int generated_max_level = 0;
    bool has_generated_mips = false;
    uint64_t gpu_bytes = 0;
};

uint64_t HashBytes(const uint8_t* data, size_t len, uint64_t seed = 14695981039346656037ull)
{
    constexpr uint64_t kPrime1 = 11400714785074694791ull;
    constexpr uint64_t kPrime2 = 14029467366897019727ull;
    constexpr uint64_t kPrime3 = 1609587929392839161ull;
    constexpr uint64_t kPrime4 = 9650029242287828579ull;
    constexpr uint64_t kPrime5 = 2870177450012600261ull;

    uint64_t h0 = seed ^ kPrime1;
    uint64_t h1 = seed + kPrime2;
    uint64_t h2 = seed ^ kPrime3;
    uint64_t h3 = seed + kPrime4;

    size_t i = 0;
    while (i + 32 <= len)
    {
        uint64_t w0 = 0;
        uint64_t w1 = 0;
        uint64_t w2 = 0;
        uint64_t w3 = 0;
        std::memcpy(&w0, data + i, 8);
        std::memcpy(&w1, data + i + 8, 8);
        std::memcpy(&w2, data + i + 16, 8);
        std::memcpy(&w3, data + i + 24, 8);
        h0 = (h0 ^ w0) * kPrime1;
        h1 = (h1 ^ w1) * kPrime2;
        h2 = (h2 ^ w2) * kPrime3;
        h3 = (h3 ^ w3) * kPrime4;
        i += 32;
    }

    uint64_t h = (h0 ^ (h1 << 1) ^ (h2 << 7) ^ (h3 >> 3)) + static_cast<uint64_t>(len) * kPrime5;
    while (i + 8 <= len)
    {
        uint64_t w = 0;
        std::memcpy(&w, data + i, 8);
        h = (h ^ w) * kPrime1;
        i += 8;
    }
    while (i < len)
    {
        h = (h ^ static_cast<uint64_t>(data[i])) * kPrime5;
        ++i;
    }

    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

uint64_t HashGuestRange(uint32_t addr, uint32_t size)
{
    if (size == 0)
    {
        return 0;
    }
    const auto& mem = runtime::GetGuestMemory();
    if (!mem.IsValidAddress(addr, size))
    {
        return 0;
    }
    const uint8_t* ptr = mem.Translate(addr);
    if (ptr == nullptr)
    {
        return 0;
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
    int msaa_samples = 0;
    int max_msaa_samples = 1;
    TextureFilterMode tex_filter_mode = TextureFilterMode::kOriginal;
    float max_anisotropy = 1.0f;
    bool driver_gpu_reported = false;
    uint64_t driver_gpu_total_bytes = 0;
    uint64_t driver_gpu_avail_bytes = 0;

    // Guest-thread recording state
    uint32_t current_target_addr = 0;
    uint32_t current_target_stride = 512;
    int current_target_fmt = 3;
    std::unordered_map<uint32_t, GuestTargetTrack> guest_targets;

    uint64_t next_tex_cache_id = 1;
    uint64_t tex_lru_tick = 1;
    uint64_t flush_epoch = 1;
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
    GLint u_tex_fbo_linear = -1;
    GLint u_tex_level_mode = -1;
    GLint u_tex_lod_bias = -1;
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
    std::unordered_map<SamplerKey, GLuint, SamplerKeyHash> ui_samplers;
    float last_applied_anisotropy = 1.0f;

    // Diagnostics & stats
    OpenGlStats stats;
    bool warned_blend_double_alpha = false;
    bool warned_blend_fix_conflict = false;
    bool warned_blend_abs_diff = false;
    bool warned_unsupported_prim = false;
};

GlContextState g_gl;

// Takes g_gl.mutex. The UI thread holds it for a whole flush, which can take
// tens of milliseconds; a guest thread that finds it held waits as a host wait,
// so the guest CPU goes to other guest threads meanwhile (a higher-priority
// sound thread woken by its timer, for example). On other threads the host
// wait is a no-op and this is a plain lock.
std::unique_lock<std::mutex> LockGl()
{
    std::unique_lock<std::mutex> lock(g_gl.mutex, std::try_to_lock);
    while (!lock.owns_lock())
    {
        {
            runtime::Scheduler::ScopedHostWait host_wait(runtime::GetScheduler());
            std::lock_guard<std::mutex> wait(g_gl.mutex);
        }
        (void)lock.try_lock();
    }
    return lock;
}

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
        "uniform int uTexFboLinear;\n"
        "uniform int uTexLevelMode;\n"
        "uniform float uTexLodBias;\n"
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
        "vec4 sampleFboTexel(int tx, int ty, ivec2 fboSize, int fboScale) {\n"
        "    int wx = wrapCoord(tx, uTexSize.x, uTexClampU);\n"
        "    int wy = wrapCoord(ty, uTexSize.y, uTexClampV);\n"
        "    int fx = clamp(wx * fboScale + (fboScale / 2), 0, fboSize.x - 1);\n"
        "    int fy = clamp(fboSize.y - 1 - (wy * fboScale + (fboScale / 2)), 0, fboSize.y - 1);\n"
        "    return texelFetch(uTex, ivec2(fx, fy), 0);\n"
        "}\n"
        "void main() {\n"
        "    vec4 c = vColor;\n"
        "    if (uClear != 0) {\n"
        "        FragColor = c;\n"
        "        return;\n"
        "    }\n"
        "    if (uTexEnable != 0) {\n"
        "        vec4 t;\n"
        "        if (uTexIsFbo != 0) {\n"
        "            ivec2 fboSize = textureSize(uTex, 0);\n"
        "            int fboScale = max(1, fboSize.x / 480);\n"
        "            if (uTexFboLinear != 0) {\n"
        "                vec2 p = vUv - vec2(0.5);\n"
        "                vec2 f = fract(p);\n"
        "                int x0 = int(floor(p.x));\n"
        "                int y0 = int(floor(p.y));\n"
        "                vec4 c00 = sampleFboTexel(x0,     y0,     fboSize, fboScale);\n"
        "                vec4 c10 = sampleFboTexel(x0 + 1, y0,     fboSize, fboScale);\n"
        "                vec4 c01 = sampleFboTexel(x0,     y0 + 1, fboSize, fboScale);\n"
        "                vec4 c11 = sampleFboTexel(x0 + 1, y0 + 1, fboSize, fboScale);\n"
        "                t = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);\n"
        "            } else {\n"
        "                t = sampleFboTexel(int(floor(vUv.x)), int(floor(vUv.y)), fboSize, fboScale);\n"
        "            }\n"
        "        } else {\n"
        "            vec2 uv = vUv / vec2(uTexSize);\n"
        "            if (uTexLevelMode == 1) {\n"
        "                t = textureLod(uTex, uv, max(uTexLodBias, 0.0));\n"
        "            } else if (uTexLodBias != 0.0) {\n"
        "                t = texture(uTex, uv, uTexLodBias);\n"
        "            } else {\n"
        "                t = texture(uTex, uv);\n"
        "            }\n"
        "        }\n"
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
    g_gl.u_tex_fbo_linear = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexFboLinear");
    g_gl.u_tex_level_mode = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexLevelMode");
    g_gl.u_tex_lod_bias = g_gl.gl.glGetUniformLocation(g_gl.program, "uTexLodBias");
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

    GLint max_samples = 1;
    g_gl.gl.glGetIntegerv(kGlMaxSamples, &max_samples);
    if (g_gl.gl.glGetError() == 0 && max_samples > 1)
    {
        g_gl.max_msaa_samples = max_samples;
    }
    else
    {
        g_gl.max_msaa_samples = 1;
    }

    GLfloat max_aniso = 1.0f;
    g_gl.gl.glGetFloatv(kGlMaxTextureMaxAnisotropy, &max_aniso);
    if (g_gl.gl.glGetError() == 0 && max_aniso > 1.0f)
    {
        g_gl.max_anisotropy = max_aniso;
    }
    else
    {
        g_gl.max_anisotropy = 1.0f;
    }

    return g_gl.vao != 0 && g_gl.vbo != 0;
}

void UiDestroyTarget(UiFboTarget& t)
{
    if (t.msaa_fbo != 0)
    {
        g_gl.gl.glDeleteFramebuffers(1, &t.msaa_fbo);
        t.msaa_fbo = 0;
    }
    if (t.msaa_color_rbo != 0)
    {
        g_gl.gl.glDeleteRenderbuffers(1, &t.msaa_color_rbo);
        t.msaa_color_rbo = 0;
    }
    if (t.msaa_depth_rbo != 0)
    {
        g_gl.gl.glDeleteRenderbuffers(1, &t.msaa_depth_rbo);
        t.msaa_depth_rbo = 0;
    }
    t.samples = 1;
    t.msaa_dirty = false;
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

void UiResolveTargetIfDirty(UiFboTarget& t)
{
    if (t.samples <= 1 || !t.msaa_dirty || t.msaa_fbo == 0 || t.fbo == 0)
    {
        return;
    }
    const int w = kScreenWidth * t.scale;
    const int h = kScreenHeight * t.scale;
    g_gl.gl.glDisable(kGlScissorTest);
    g_gl.gl.glBindFramebuffer(kGlReadFramebuffer, t.msaa_fbo);
    g_gl.gl.glBindFramebuffer(kGlDrawFramebuffer, t.fbo);
    g_gl.gl.glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, kGlColorBufferBit, kGlNearest);
    g_gl.gl.glBindFramebuffer(kGlFramebuffer, 0);
    t.msaa_dirty = false;
}

void UiShutdownResources()
{
    if (g_gl.gl.glDeleteFramebuffers == nullptr)
    {
        return;
    }
    auto lock = LockGl();
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

    for (auto& kv : g_gl.ui_samplers)
    {
        if (kv.second != 0)
        {
            g_gl.gl.glDeleteSamplers(1, &kv.second);
        }
    }
    g_gl.ui_samplers.clear();

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

UiFboTarget& UiGetOrCreateTarget(uint32_t addr, int scale, int req_samples)
{
    const int eff_samples = (req_samples <= 1 || g_gl.max_msaa_samples <= 1)
                                ? 1
                                : std::min(req_samples, g_gl.max_msaa_samples);
    auto& t = g_gl.ui_targets[addr];
    if (t.fbo != 0 && t.scale == scale && t.samples == eff_samples)
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
    t.samples = 1;
    t.msaa_dirty = false;

    g_gl.gl.glDisable(kGlScissorTest);
    g_gl.gl.glColorMask(kGlTrue, kGlTrue, kGlTrue, kGlTrue);
    g_gl.gl.glDepthMask(kGlTrue);
    g_gl.gl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    g_gl.gl.glClear(kGlColorBufferBit | kGlDepthBufferBit | kGlStencilBufferBit);

    if (eff_samples > 1)
    {
        g_gl.gl.glGenFramebuffers(1, &t.msaa_fbo);
        g_gl.gl.glGenRenderbuffers(1, &t.msaa_color_rbo);
        g_gl.gl.glGenRenderbuffers(1, &t.msaa_depth_rbo);

        g_gl.gl.glBindRenderbuffer(kGlRenderbuffer, t.msaa_color_rbo);
        g_gl.gl.glRenderbufferStorageMultisample(kGlRenderbuffer, eff_samples, kGlRgba8, w, h);
        g_gl.gl.glBindRenderbuffer(kGlRenderbuffer, t.msaa_depth_rbo);
        g_gl.gl.glRenderbufferStorageMultisample(kGlRenderbuffer, eff_samples, kGlDepth24Stencil8, w, h);

        g_gl.gl.glBindFramebuffer(kGlFramebuffer, t.msaa_fbo);
        g_gl.gl.glFramebufferRenderbuffer(kGlFramebuffer, kGlColorAttachment0, kGlRenderbuffer, t.msaa_color_rbo);
        g_gl.gl.glFramebufferRenderbuffer(kGlFramebuffer, kGlDepthStencilAttachment, kGlRenderbuffer, t.msaa_depth_rbo);
        if (g_gl.gl.glCheckFramebufferStatus(kGlFramebuffer) == kGlFramebufferComplete)
        {
            t.samples = eff_samples;
            g_gl.gl.glClear(kGlColorBufferBit | kGlDepthBufferBit | kGlStencilBufferBit);
        }
        else
        {
            g_gl.gl.glDeleteFramebuffers(1, &t.msaa_fbo);
            g_gl.gl.glDeleteRenderbuffers(1, &t.msaa_color_rbo);
            g_gl.gl.glDeleteRenderbuffers(1, &t.msaa_depth_rbo);
            t.msaa_fbo = 0;
            t.msaa_color_rbo = 0;
            t.msaa_depth_rbo = 0;
        }
    }

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

int ComputeFullMipLevels(int w, int h)
{
    int m = std::max(w, h);
    int levels = 0;
    while (m > 1)
    {
        m >>= 1;
        ++levels;
    }
    return levels;
}

uint64_t ComputeMipChainBytes(int w, int h, int max_level)
{
    uint64_t total = 0;
    for (int lvl = 0; lvl <= max_level; ++lvl)
    {
        const int lw = std::max(1, w >> lvl);
        const int lh = std::max(1, h >> lvl);
        total += static_cast<uint64_t>(lw) * static_cast<uint64_t>(lh) * 4ull;
    }
    return total;
}

GLenum MapOriginalMinFilter(int tflt_min, bool has_mips)
{
    if (!has_mips)
    {
        return (tflt_min & 1) ? kGlLinear : kGlNearest;
    }
    switch (tflt_min & 7)
    {
    case 0: return kGlNearest;
    case 1: return kGlLinear;
    case 4: return kGlNearestMipmapNearest;
    case 5: return kGlLinearMipmapNearest;
    case 6: return kGlNearestMipmapLinear;
    case 7: return kGlLinearMipmapLinear;
    default: return (tflt_min & 1) ? kGlLinear : kGlNearest;
    }
}

bool IsAnisotropicMode(TextureFilterMode mode)
{
    return mode == TextureFilterMode::kAniso2x ||
           mode == TextureFilterMode::kAniso4x ||
           mode == TextureFilterMode::kAniso8x ||
           mode == TextureFilterMode::kAniso16x;
}

float RequestedAnisotropy(TextureFilterMode mode)
{
    switch (mode)
    {
    case TextureFilterMode::kAniso2x: return 2.0f;
    case TextureFilterMode::kAniso4x: return 4.0f;
    case TextureFilterMode::kAniso8x: return 8.0f;
    case TextureFilterMode::kAniso16x: return 16.0f;
    default: return 1.0f;
    }
}

GLuint UiGetOrCreateSampler(const SamplerKey& key)
{
    auto it = g_gl.ui_samplers.find(key);
    if (it != g_gl.ui_samplers.end())
    {
        return it->second;
    }
    GLuint sampler = 0;
    g_gl.gl.glGenSamplers(1, &sampler);
    g_gl.gl.glSamplerParameteri(sampler, kGlTextureMinFilter, static_cast<GLint>(key.min_filter));
    g_gl.gl.glSamplerParameteri(sampler, kGlTextureMagFilter, static_cast<GLint>(key.mag_filter));
    g_gl.gl.glSamplerParameteri(sampler, kGlTextureWrapS, static_cast<GLint>(key.wrap_s));
    g_gl.gl.glSamplerParameteri(sampler, kGlTextureWrapT, static_cast<GLint>(key.wrap_t));
    if (g_gl.max_anisotropy > 1.0f)
    {
        const float aniso = std::min(static_cast<float>(key.aniso_int), g_gl.max_anisotropy);
        g_gl.gl.glSamplerParameterf(sampler, kGlTextureMaxAnisotropy, aniso);
    }
    g_gl.ui_samplers.emplace(key, sampler);
    return sampler;
}

void UiExecuteFlush(std::unique_ptr<FlushPayload> payload, int scale, int req_samples, TextureFilterMode tex_filter)
{
    if (!payload)
    {
        return;
    }

    auto lock = LockGl();

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
        if (up.levels.empty())
        {
            continue;
        }
        auto& entry = g_gl.ui_textures[up.cache_id];
        if (entry.tex == 0)
        {
            g_gl.gl.glGenTextures(1, &entry.tex);
        }
        const int lvl0_w = up.levels[0].width;
        const int lvl0_h = up.levels[0].height;
        const int supplied_max = static_cast<int>(up.levels.size()) - 1;

        g_gl.gl.glActiveTexture(kGlTexture0);
        g_gl.gl.glBindTexture(kGlTexture2d, entry.tex);
        g_gl.gl.glPixelStorei(kGlUnpackAlignment, 4);
        const bool same_shape = (entry.width == lvl0_w && entry.height == lvl0_h &&
                                 entry.supplied_max_level == supplied_max);
        for (int lvl = 0; lvl <= supplied_max; ++lvl)
        {
            const auto& md = up.levels[static_cast<size_t>(lvl)];
            if (!same_shape)
            {
                g_gl.gl.glTexImage2D(kGlTexture2d, lvl, static_cast<GLint>(kGlRgba8),
                                     md.width, md.height, 0, kGlRgba, kGlUnsignedByte, md.rgba.data());
            }
            else
            {
                g_gl.gl.glTexSubImage2D(kGlTexture2d, lvl, 0, 0, md.width, md.height,
                                        kGlRgba, kGlUnsignedByte, md.rgba.data());
            }
        }
        g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureBaseLevel, 0);
        g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureMaxLevel, supplied_max);

        entry.width = lvl0_w;
        entry.height = lvl0_h;
        entry.supplied_max_level = supplied_max;
        entry.uploaded_max_level = supplied_max;
        entry.generated_max_level = 0;
        entry.has_generated_mips = false;
        entry.gpu_bytes = ComputeMipChainBytes(lvl0_w, lvl0_h, supplied_max);
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
    GLuint bound_draw_fbo = 0;
    for (const auto& batch : payload->batches)
    {
        const PipelineState& s = batch.state;
        if (s.target_addr == 0)
        {
            continue;
        }
        if (s.target_addr != bound_target)
        {
            UiFboTarget& fbo = UiGetOrCreateTarget(s.target_addr, scale, req_samples);
            bound_draw_fbo = (fbo.samples > 1 && fbo.msaa_fbo != 0) ? fbo.msaa_fbo : fbo.fbo;
            if (fbo.samples > 1)
            {
                fbo.msaa_dirty = true;
            }
            g_gl.gl.glBindFramebuffer(kGlFramebuffer, bound_draw_fbo);
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
            UiTexEntry* tex_entry = nullptr;
            if (s.fbo_tex_addr != 0)
            {
                auto fit = g_gl.ui_targets.find(s.fbo_tex_addr);
                if (fit != g_gl.ui_targets.end() && fit->second.color_tex != 0)
                {
                    if (fit->second.msaa_dirty)
                    {
                        UiResolveTargetIfDirty(fit->second);
                        g_gl.gl.glBindFramebuffer(kGlFramebuffer, bound_draw_fbo);
                        g_gl.gl.glEnable(kGlScissorTest);
                    }
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
                    tex_entry = &tit->second;
                }
            }
            g_gl.gl.glActiveTexture(kGlTexture0);
            g_gl.gl.glBindTexture(kGlTexture2d, tex_id);

            int fbo_linear = 0;
            if (tex_id != 0 && is_fbo != 0)
            {
                g_gl.gl.glBindSampler(0, 0);
                if (tex_filter != TextureFilterMode::kOriginal)
                {
                    fbo_linear = 1;
                }
                else
                {
                    fbo_linear = ((s.tex_mag_filter & 1) != 0 || (s.tex_min_filter & 1) != 0) ? 1 : 0;
                }
            }
            else if (tex_id != 0 && tex_entry != nullptr)
            {
                const bool is_aniso = IsAnisotropicMode(tex_filter);
                bool has_mips = false;
                if (tex_entry->supplied_max_level >= 1)
                {
                    has_mips = true;
                }
                else if (is_aniso)
                {
                    const int full_max = ComputeFullMipLevels(tex_entry->width, tex_entry->height);
                    if (full_max >= 1)
                    {
                        if (!tex_entry->has_generated_mips)
                        {
                            g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureMaxLevel, full_max);
                            g_gl.gl.glGenerateMipmap(kGlTexture2d);
                            tex_entry->has_generated_mips = true;
                            tex_entry->generated_max_level = full_max;
                            tex_entry->uploaded_max_level = full_max;
                            tex_entry->gpu_bytes = ComputeMipChainBytes(tex_entry->width, tex_entry->height, full_max);
                        }
                        else if (tex_entry->uploaded_max_level != tex_entry->generated_max_level)
                        {
                            g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureMaxLevel, tex_entry->generated_max_level);
                            tex_entry->uploaded_max_level = tex_entry->generated_max_level;
                        }
                        has_mips = true;
                    }
                }
                else if (tex_entry->uploaded_max_level != 0)
                {
                    g_gl.gl.glTexParameteri(kGlTexture2d, kGlTextureMaxLevel, 0);
                    tex_entry->uploaded_max_level = 0;
                }

                SamplerKey sk;
                sk.wrap_s = s.tex_clamp_u ? kGlClampToEdge : kGlRepeat;
                sk.wrap_t = s.tex_clamp_v ? kGlClampToEdge : kGlRepeat;
                if (tex_filter == TextureFilterMode::kOriginal)
                {
                    sk.mag_filter = (s.tex_mag_filter & 1) ? kGlLinear : kGlNearest;
                    sk.min_filter = MapOriginalMinFilter(s.tex_min_filter, has_mips);
                    sk.aniso_int = 1;
                }
                else
                {
                    sk.mag_filter = kGlLinear;
                    sk.min_filter = has_mips ? kGlLinearMipmapLinear : kGlLinear;
                    const float req_aniso = is_aniso ? RequestedAnisotropy(tex_filter) : 1.0f;
                    const float eff_aniso = (g_gl.max_anisotropy > 1.0f)
                                                ? std::min(req_aniso, g_gl.max_anisotropy)
                                                : 1.0f;
                    sk.aniso_int = std::max(1, static_cast<int>(std::round(eff_aniso)));
                }
                const GLuint sampler = UiGetOrCreateSampler(sk);
                g_gl.gl.glBindSampler(0, sampler);
                if (g_gl.max_anisotropy > 1.0f)
                {
                    GLfloat applied = 1.0f;
                    g_gl.gl.glGetSamplerParameterfv(sampler, kGlTextureMaxAnisotropy, &applied);
                    g_gl.last_applied_anisotropy = applied;
                }
                else
                {
                    g_gl.last_applied_anisotropy = 1.0f;
                }
            }
            else
            {
                g_gl.gl.glBindSampler(0, 0);
            }

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
            g_gl.gl.glUniform1i(g_gl.u_tex_fbo_linear, fbo_linear);
            g_gl.gl.glUniform1i(g_gl.u_tex_level_mode, s.tex_level_mode);
            g_gl.gl.glUniform1f(g_gl.u_tex_lod_bias, s.tex_lod_bias);
        }
        else
        {
            g_gl.gl.glBindSampler(0, 0);
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
    g_gl.gl.glBindSampler(0, 0);
    g_gl.gl.glBindTexture(kGlTexture2d, 0);
    g_gl.gl.glBindFramebuffer(kGlFramebuffer, 0);
    g_gl.gl.glUseProgram(0);

    for (auto& kv : g_gl.ui_targets)
    {
        UiResolveTargetIfDirty(kv.second);
    }

    GLint total_kb = 0;
    GLint avail_kb = 0;
    g_gl.gl.glGetIntegerv(kGlGpuMemInfoTotalKbNvx, &total_kb);
    if (g_gl.gl.glGetError() == 0 && total_kb > 0)
    {
        g_gl.gl.glGetIntegerv(kGlGpuMemInfoAvailKbNvx, &avail_kb);
        if (g_gl.gl.glGetError() == 0 && avail_kb >= 0)
        {
            g_gl.driver_gpu_reported = true;
            g_gl.driver_gpu_total_bytes = static_cast<uint64_t>(total_kb) * 1024ull;
            g_gl.driver_gpu_avail_bytes = static_cast<uint64_t>(avail_kb) * 1024ull;
        }
    }
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

    int effective_max_level = 0;
    const int req_max_level = std::clamp(st.tex_max_level, 0, DrawState::kMaxTexLevels - 1);
    for (int lvl = 1; lvl <= req_max_level; ++lvl)
    {
        const int expected_w = std::max(1, st.tex_w >> lvl);
        const int expected_h = std::max(1, st.tex_h >> lvl);
        if (st.tex_level_addr[lvl] == 0 || st.tex_level_bw[lvl] <= 0 ||
            st.tex_level_w[lvl] != expected_w || st.tex_level_h[lvl] != expected_h)
        {
            break;
        }
        effective_max_level = lvl;
    }

    TextureKey key;
    key.addr = st.tex_addr;
    key.width = st.tex_w;
    key.height = st.tex_h;
    key.bw = st.tex_bw;
    key.fmt = st.tex_fmt;
    key.swizzle = st.tex_swizzle;
    key.max_level = effective_max_level;
    for (int lvl = 1; lvl <= effective_max_level; ++lvl)
    {
        key.level_addr[static_cast<size_t>(lvl)] = st.tex_level_addr[lvl];
        key.level_bw[static_cast<size_t>(lvl)] = st.tex_level_bw[lvl];
        key.level_w[static_cast<size_t>(lvl)] = st.tex_level_w[lvl];
        key.level_h[static_cast<size_t>(lvl)] = st.tex_level_h[lvl];
    }
    key.clut_fmt = st.clut_fmt;
    key.clut_start = st.clut_start;
    key.clut_shift = st.clut_shift;
    key.clut_mask = st.clut_mask;

    uint64_t clut_hash = 0;
    if (st.tex_fmt >= kTpsmT4 && st.tex_fmt <= kTpsmT32 && st.clut != nullptr)
    {
        const size_t clut_bytes = (st.clut_fmt == 3) ? (256u * 4u) : (512u * 2u);
        clut_hash = HashBytes(st.clut, clut_bytes);
    }

    auto it = g_gl.guest_tex_cache.find(key);
    if (it != g_gl.guest_tex_cache.end() &&
        it->second.verified_epoch == g_gl.flush_epoch &&
        it->second.clut_hash == clut_hash)
    {
        it->second.lru_tick = ++g_gl.tex_lru_tick;
        ++g_gl.stats.texture_cache_hits;
        return it->second.cache_id;
    }

    uint64_t content_hash = HashGuestRange(st.tex_addr, data_bytes);
    g_gl.stats.texture_bytes_hashed += data_bytes;
    for (int lvl = 1; lvl <= effective_max_level; ++lvl)
    {
        const uint32_t lvl_bytes = ComputeTextureLevelDataSizeBytes(st, lvl);
        const uint64_t lvl_hash = HashGuestRange(st.tex_level_addr[lvl], lvl_bytes);
        content_hash ^= lvl_hash + 0x9e3779b97f4a7c15ull + (content_hash << 6) + (content_hash >> 2);
        g_gl.stats.texture_bytes_hashed += lvl_bytes;
    }

    if (it != g_gl.guest_tex_cache.end() &&
        it->second.content_hash == content_hash &&
        it->second.clut_hash == clut_hash)
    {
        it->second.verified_epoch = g_gl.flush_epoch;
        it->second.lru_tick = ++g_gl.tex_lru_tick;
        ++g_gl.stats.texture_cache_hits;
        return it->second.cache_id;
    }

    uint64_t cache_id = 0;
    if (it != g_gl.guest_tex_cache.end())
    {
        cache_id = it->second.cache_id;
        it->second.content_hash = content_hash;
        it->second.clut_hash = clut_hash;
        it->second.verified_epoch = g_gl.flush_epoch;
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
        g_gl.guest_tex_cache.emplace(
            key,
            GuestTexEntry{cache_id, content_hash, clut_hash, g_gl.flush_epoch, ++g_gl.tex_lru_tick});
    }

    TextureUpload up;
    up.cache_id = cache_id;
    up.levels.resize(static_cast<size_t>(effective_max_level + 1));
    for (int lvl = 0; lvl <= effective_max_level; ++lvl)
    {
        const int lw = (lvl == 0) ? st.tex_w : st.tex_level_w[lvl];
        const int lh = (lvl == 0) ? st.tex_h : st.tex_level_h[lvl];
        auto& md = up.levels[static_cast<size_t>(lvl)];
        md.width = lw;
        md.height = lh;
        md.rgba.resize(static_cast<size_t>(lw) * static_cast<size_t>(lh) * 4u);
        DecodeTextureLevelRgba8(st, lvl, md.rgba.data(), nullptr);
    }
    flush.uploads.push_back(std::move(up));
    return cache_id;
}

void FlushPendingLocked()
{
    // A texture hashed in the current epoch is reused without hashing again.
    // The epoch advances at every flush and whenever guest code may have run
    // (GlGuestMemoryMayHaveChanged), so new contents are always seen.
    ++g_gl.flush_epoch;

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
    const int req_samples = g_gl.msaa_samples;
    const TextureFilterMode tex_filter = g_gl.tex_filter_mode;
    display->SubmitRenderJob([shared_payload, scale, req_samples, tex_filter]()
    {
        UiExecuteFlush(std::make_unique<FlushPayload>(std::move(*shared_payload)), scale, req_samples, tex_filter);
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
    auto lock = LockGl();
    g_gl.initialized = true;
    g_gl.pending_flush.reset();
    return 0;
}

void GlShutdown()
{
    auto* display = platform::GetDisplay();
    {
        auto lock = LockGl();
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
    auto lock = LockGl();
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
    auto lock = LockGl();
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
        ps.tex_min_filter = st->tex_min_filter;
        ps.tex_mag_filter = st->tex_mag_filter;
        ps.tex_level_mode = st->tex_level_mode;
        ps.tex_lod_bias = st->tex_lod_bias;
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
    auto lock = LockGl();
    FlushPendingLocked();
}

void GlPresent()
{
    auto lock = LockGl();
    FlushPendingLocked();
}

void GlGuestMemoryMayHaveChanged()
{
    auto lock = LockGl();
    ++g_gl.flush_epoch;
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
    GlGuestMemoryMayHaveChanged,
};

void SetInternalScale(int scale)
{
    auto lock = LockGl();
    g_gl.internal_scale = std::clamp(scale, 1, 8);
}

int GetInternalScale()
{
    auto lock = LockGl();
    return g_gl.internal_scale;
}

bool HasGlTargetForAddress(uint32_t addr)
{
    if (addr == 0)
    {
        return false;
    }
    auto lock = LockGl();
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
        auto lock = LockGl();
        FlushPendingLocked();
        auto git = g_gl.guest_targets.find(addr);
        if (git != g_gl.guest_targets.end())
        {
            CheckCpuWriteOverlapLocked(git->second);
        }
    }
    {
        runtime::Scheduler::ScopedHostWait host_wait(runtime::GetScheduler());
        display->WaitRenderIdle();
    }

    uint32_t tex_id = 0;
    int w = 0;
    int h = 0;
    {
        auto lock = LockGl();
        auto it = g_gl.ui_targets.find(addr);
        if (it == g_gl.ui_targets.end() || it->second.color_tex == 0)
        {
            return false;
        }
        tex_id = it->second.color_tex;
        w = kScreenWidth * it->second.scale;
        h = kScreenHeight * it->second.scale;
    }
    {
        runtime::Scheduler::ScopedHostWait host_wait(runtime::GetScheduler());
        display->PresentGlTexture(tex_id, w, h);
    }
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
        auto lock = LockGl();
        FlushPendingLocked();
    }

    const int clamped_out_scale = std::clamp(output_scale, 1, 8);
    bool ok = false;
    display->SubmitRenderJob([addr, clamped_out_scale, &out_rgba, &out_w, &out_h, &ok]()
    {
        auto lock = LockGl();
        auto it = g_gl.ui_targets.find(addr);
        if (it == g_gl.ui_targets.end() || it->second.fbo == 0)
        {
            ok = false;
            return;
        }
        UiResolveTargetIfDirty(it->second);
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
    auto lock = LockGl();
    return g_gl.stats;
}

void ResetOpenGlStats()
{
    auto lock = LockGl();
    g_gl.stats = OpenGlStats{};
    g_gl.warned_blend_double_alpha = false;
    g_gl.warned_blend_fix_conflict = false;
    g_gl.warned_blend_abs_diff = false;
    g_gl.warned_unsupported_prim = false;
}

void SetMsaaSamples(int samples)
{
    auto lock = LockGl();
    if (samples <= 1)
    {
        g_gl.msaa_samples = 0;
    }
    else if (samples <= 2)
    {
        g_gl.msaa_samples = 2;
    }
    else if (samples <= 4)
    {
        g_gl.msaa_samples = 4;
    }
    else
    {
        g_gl.msaa_samples = 8;
    }
}

int GetMsaaSamples()
{
    auto lock = LockGl();
    return g_gl.msaa_samples;
}

int GetEffectiveMsaaSamples()
{
    auto lock = LockGl();
    if (g_gl.msaa_samples <= 1 || g_gl.max_msaa_samples <= 1)
    {
        return 0;
    }
    return std::min(g_gl.msaa_samples, g_gl.max_msaa_samples);
}

int GetMaxMsaaSamples()
{
    auto lock = LockGl();
    return g_gl.max_msaa_samples;
}

void SetTextureFilterMode(TextureFilterMode mode)
{
    auto lock = LockGl();
    g_gl.tex_filter_mode = mode;
}

TextureFilterMode GetTextureFilterMode()
{
    auto lock = LockGl();
    return g_gl.tex_filter_mode;
}

float GetMaxAnisotropy()
{
    auto lock = LockGl();
    return g_gl.max_anisotropy;
}

GpuMemoryInfo GetGpuMemoryInfo()
{
    auto lock = LockGl();
    GpuMemoryInfo info;
    info.driver_reported = g_gl.driver_gpu_reported;
    info.total_bytes = g_gl.driver_gpu_total_bytes;
    info.available_bytes = g_gl.driver_gpu_avail_bytes;
    for (const auto& kv : g_gl.ui_textures)
    {
        if (kv.second.tex != 0 && kv.second.width > 0 && kv.second.height > 0)
        {
            info.hle_textures_bytes += kv.second.gpu_bytes;
        }
    }
    for (const auto& kv : g_gl.ui_targets)
    {
        if (kv.second.fbo != 0)
        {
            const uint64_t px = static_cast<uint64_t>(kScreenWidth * kv.second.scale) *
                                static_cast<uint64_t>(kScreenHeight * kv.second.scale);
            // Color RGBA8 (4 bytes) + Depth24Stencil8 (4 bytes)
            info.hle_targets_bytes += px * 8ull;
            if (kv.second.samples > 1 && kv.second.msaa_fbo != 0)
            {
                info.hle_targets_bytes += px * 8ull * static_cast<uint64_t>(kv.second.samples);
            }
        }
    }
    return info;
}

void LockGlMutexForTest()
{
    g_gl.mutex.lock();
}

void UnlockGlMutexForTest()
{
    g_gl.mutex.unlock();
}

float GetLastAppliedAnisotropyForTest()
{
    auto lock = LockGl();
    return g_gl.last_applied_anisotropy;
}

} // namespace repsp::gpu
