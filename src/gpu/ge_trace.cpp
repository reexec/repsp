#include "ge_trace.h"
#include "texture_decode.h"
#include "repsp/runtime/guest_memory.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace repsp::gpu::detail
{

namespace
{

struct TextureKey
{
    uint32_t addr = 0;
    int fmt = 0;
    int w = 0;
    int h = 0;
    int bw = 0;
    int swizzle = 0;
    uint64_t tex_hash = 0;
    int clut_fmt = 0;
    int clut_shift = 0;
    int clut_mask = 0;
    int clut_start = 0;
    uint64_t clut_hash = 0;

    bool operator==(const TextureKey& o) const
    {
        return addr == o.addr && fmt == o.fmt && w == o.w && h == o.h &&
               bw == o.bw && swizzle == o.swizzle && tex_hash == o.tex_hash &&
               clut_fmt == o.clut_fmt && clut_shift == o.clut_shift &&
               clut_mask == o.clut_mask && clut_start == o.clut_start &&
               clut_hash == o.clut_hash;
    }
};

struct TextureKeyHash
{
    size_t operator()(const TextureKey& k) const noexcept
    {
        uint64_t h = 14695981039346656037ull;
        auto mix = [&h](uint64_t v)
        {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(k.addr);
        mix(static_cast<uint64_t>(k.fmt));
        mix(static_cast<uint64_t>(k.w));
        mix(static_cast<uint64_t>(k.h));
        mix(static_cast<uint64_t>(k.bw));
        mix(static_cast<uint64_t>(k.swizzle));
        mix(k.tex_hash);
        mix(static_cast<uint64_t>(k.clut_fmt));
        mix(static_cast<uint64_t>(k.clut_shift));
        mix(static_cast<uint64_t>(k.clut_mask));
        mix(static_cast<uint64_t>(k.clut_start));
        mix(k.clut_hash);
        return static_cast<size_t>(h);
    }
};

uint64_t Fnv1a64(const uint8_t* data, size_t size)
{
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i)
    {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

const char* TexFmtName(int fmt)
{
    static constexpr const char* kNames[11] = {
        "565", "5551", "4444", "8888",
        "T4", "T8", "T16", "T32",
        "DXT1", "DXT3", "DXT5"
    };
    return (fmt >= 0 && fmt < 11) ? kNames[fmt] : "?";
}

void WriteBmpCompositedOverBlack(const std::filesystem::path& path, const uint8_t* rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || !rgba)
    {
        return;
    }
    FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f)
    {
        return;
    }
    const uint32_t pixel_bytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4u;
    const uint32_t file_size = 54u + pixel_bytes;

    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(header + 2, &file_size, 4);
    const uint32_t data_offset = 54u;
    std::memcpy(header + 10, &data_offset, 4);
    const uint32_t dib_size = 40u;
    std::memcpy(header + 14, &dib_size, 4);
    const int32_t w32 = width;
    const int32_t h32 = -height; // top-down
    std::memcpy(header + 18, &w32, 4);
    std::memcpy(header + 22, &h32, 4);
    const uint16_t planes = 1;
    const uint16_t bpp = 32;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &pixel_bytes, 4);
    std::fwrite(header, 1, sizeof(header), f);

    const size_t n_px = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> bgra(n_px * 4u);
    for (size_t i = 0; i < n_px; ++i)
    {
        const uint32_t r = rgba[i * 4 + 0];
        const uint32_t g = rgba[i * 4 + 1];
        const uint32_t b = rgba[i * 4 + 2];
        const uint32_t a = rgba[i * 4 + 3];
        bgra[i * 4 + 0] = static_cast<uint8_t>((b * a) / 255u);
        bgra[i * 4 + 1] = static_cast<uint8_t>((g * a) / 255u);
        bgra[i * 4 + 2] = static_cast<uint8_t>((r * a) / 255u);
        bgra[i * 4 + 3] = 255u;
    }
    std::fwrite(bgra.data(), 1, bgra.size(), f);
    std::fclose(f);
}

void WriteBinaryFile(const std::filesystem::path& path, const uint8_t* data, size_t size)
{
    FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f)
    {
        return;
    }
    if (size > 0 && data)
    {
        std::fwrite(data, 1, size, f);
    }
    std::fclose(f);
}

bool ParseFrameRange(const char* s, uint64_t* first_out, uint64_t* last_out)
{
    if (!s || s[0] == '\0')
    {
        return false;
    }
    char* end1 = nullptr;
    const unsigned long long v1 = std::strtoull(s, &end1, 10);
    if (end1 == s)
    {
        return false;
    }
    if (*end1 == '\0')
    {
        *first_out = static_cast<uint64_t>(v1);
        *last_out = static_cast<uint64_t>(v1);
        return true;
    }
    if (*end1 == '-')
    {
        char* end2 = nullptr;
        const unsigned long long v2 = std::strtoull(end1 + 1, &end2, 10);
        if (end2 == end1 + 1 || *end2 != '\0' || v2 < v1)
        {
            return false;
        }
        *first_out = static_cast<uint64_t>(v1);
        *last_out = static_cast<uint64_t>(v2);
        return true;
    }
    return false;
}

struct TraceState
{
    std::mutex mutex;
    bool initialized_from_env = false;
    bool enabled = false;
    std::string dir;
    uint64_t first_frame = 0;
    uint64_t last_frame = 0;
    uint64_t current_frame = 0;
    uint64_t draw_index = 0;
    FILE* log_file = nullptr;
    bool dir_created = false;
    uint32_t next_tex_id = 0;
    std::unordered_map<TextureKey, uint32_t, TextureKeyHash> dumped_textures;

    void CloseLogLocked()
    {
        if (log_file)
        {
            std::fclose(log_file);
            log_file = nullptr;
        }
    }

    ~TraceState()
    {
        CloseLogLocked();
    }
};

// Fast flag checked before acquiring any lock.
bool g_trace_enabled_fast = false;

TraceState& GetState()
{
    static TraceState state;
    return state;
}

void EnsureEnvLoaded()
{
    auto& st = GetState();
    if (st.initialized_from_env)
    {
        return;
    }
    GeTraceReloadFromEnv();
}

} // namespace

bool GeTraceIsEnabled()
{
    return g_trace_enabled_fast;
}

void GeTraceConfigure(const char* dir, uint64_t first_frame, uint64_t last_frame)
{
    auto& st = GetState();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.CloseLogLocked();
    st.dumped_textures.clear();
    st.next_tex_id = 0;
    st.draw_index = 0;
    st.dir_created = false;
    st.initialized_from_env = true;

    if (!dir || dir[0] == '\0' || last_frame < first_frame)
    {
        st.enabled = false;
        st.dir.clear();
        st.first_frame = 0;
        st.last_frame = 0;
        g_trace_enabled_fast = false;
        return;
    }

    st.dir = dir;
    st.first_frame = first_frame;
    st.last_frame = last_frame;
    st.enabled = true;
    g_trace_enabled_fast = true;
}

void GeTraceReloadFromEnv()
{
    const char* env_dir = std::getenv("REPSP_GE_TRACE");
    if (!env_dir || env_dir[0] == '\0')
    {
        GeTraceConfigure(nullptr, 0, 0);
        return;
    }
    uint64_t first = 0;
    uint64_t last = std::numeric_limits<uint64_t>::max();
    const char* env_frames = std::getenv("REPSP_GE_TRACE_FRAMES");
    if (env_frames && env_frames[0] != '\0')
    {
        if (!ParseFrameRange(env_frames, &first, &last))
        {
            GeTraceConfigure(nullptr, 0, 0);
            return;
        }
    }
    GeTraceConfigure(env_dir, first, last);
}

void GeTraceSetFrame(uint64_t frame)
{
    EnsureEnvLoaded();
    auto& st = GetState();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.current_frame = frame;
    if (st.log_file && frame > st.last_frame)
    {
        std::fflush(st.log_file);
    }
}

void GeTraceRecordDraw(uint64_t list_num, const char* prim_name, uint32_t vtype,
                       const Vertex* verts, uint32_t count,
                       const DrawState& ds, const TransformState& xf,
                       uint32_t clut_loaded_bytes)
{
    if (!g_trace_enabled_fast)
    {
        return;
    }

    auto& st = GetState();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.enabled || st.current_frame < st.first_frame || st.current_frame > st.last_frame)
    {
        return;
    }

    if (!st.dir_created)
    {
        std::error_code ec;
        std::filesystem::create_directories(st.dir, ec);
        st.dir_created = true;
    }
    if (!st.log_file)
    {
        const std::filesystem::path log_path = std::filesystem::path(st.dir) / "ge_trace.txt";
        st.log_file = std::fopen(log_path.string().c_str(), "wb");
        if (!st.log_file)
        {
            return;
        }
    }

    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    float min_u = 0.0f, max_u = 0.0f, min_v = 0.0f, max_v = 0.0f;
    if (verts && count > 0)
    {
        min_x = max_x = verts[0].x;
        min_y = max_y = verts[0].y;
        min_u = max_u = verts[0].u;
        min_v = max_v = verts[0].v;
        for (uint32_t i = 1; i < count; ++i)
        {
            min_x = std::min(min_x, verts[i].x);
            max_x = std::max(max_x, verts[i].x);
            min_y = std::min(min_y, verts[i].y);
            max_y = std::max(max_y, verts[i].y);
            min_u = std::min(min_u, verts[i].u);
            max_u = std::max(max_u, verts[i].u);
            min_v = std::min(min_v, verts[i].v);
            max_v = std::max(max_v, verts[i].v);
        }
    }

    uint64_t tex_hash = 0;
    uint64_t clut_hash = 0;
    uint32_t tex_id = 0;
    bool has_tex_dump = false;
    const uint32_t clut_entry_bytes = (ds.clut_fmt == 3) ? 4u : 2u;
    const uint32_t clut_entries = clut_loaded_bytes / clut_entry_bytes;

    if (ds.tex_enable)
    {
        const uint32_t tex_size = ComputeTextureDataSizeBytes(ds);
        std::vector<uint8_t> raw_tex(tex_size);
        auto& mem = runtime::GetGuestMemory();
        for (uint32_t i = 0; i < tex_size; ++i)
        {
            raw_tex[i] = mem.ReadU8(ds.tex_addr + i);
        }
        tex_hash = Fnv1a64(raw_tex.data(), raw_tex.size());

        const bool indexed = (ds.tex_fmt >= kTpsmT4 && ds.tex_fmt <= kTpsmT32);
        if (indexed && ds.clut && clut_loaded_bytes > 0)
        {
            clut_hash = Fnv1a64(ds.clut, clut_loaded_bytes);
        }

        TextureKey key;
        key.addr = ds.tex_addr;
        key.fmt = ds.tex_fmt;
        key.w = ds.tex_w;
        key.h = ds.tex_h;
        key.bw = ds.tex_bw;
        key.swizzle = ds.tex_swizzle;
        key.tex_hash = tex_hash;
        if (indexed)
        {
            key.clut_fmt = ds.clut_fmt;
            key.clut_shift = ds.clut_shift;
            key.clut_mask = ds.clut_mask;
            key.clut_start = ds.clut_start;
            key.clut_hash = clut_hash;
        }

        auto it = st.dumped_textures.find(key);
        if (it != st.dumped_textures.end())
        {
            tex_id = it->second;
            has_tex_dump = true;
        }
        else
        {
            tex_id = st.next_tex_id++;
            st.dumped_textures.emplace(key, tex_id);
            has_tex_dump = true;

            char base[64];
            std::snprintf(base, sizeof(base), "tex_%04u", tex_id);
            const std::filesystem::path dir_path(st.dir);

            WriteBinaryFile(dir_path / (std::string(base) + "_raw.bin"), raw_tex.data(), raw_tex.size());
            if (indexed && ds.clut && clut_loaded_bytes > 0)
            {
                WriteBinaryFile(dir_path / (std::string(base) + "_clut.bin"), ds.clut, clut_loaded_bytes);
            }
            if (ds.tex_w > 0 && ds.tex_h > 0)
            {
                std::vector<uint8_t> rgba(static_cast<size_t>(ds.tex_w) * static_cast<size_t>(ds.tex_h) * 4u);
                DecodeTextureRgba8(ds, rgba.data(), nullptr);
                WriteBmpCompositedOverBlack(dir_path / (std::string(base) + "_dec.bmp"),
                                            rgba.data(), ds.tex_w, ds.tex_h);
            }
        }
    }

    const uint32_t through = (vtype >> 23) & 1u;
    const uint32_t uv_fmt = vtype & 3u;
    const uint32_t col_fmt = (vtype >> 2) & 7u;

    std::fprintf(
        st.log_file,
        "f=%" PRIu64 " list=%" PRIu64 " draw=%" PRIu64 " prim=%s cnt=%u "
        "vtype=0x%06X(thr=%u,uv=%u,col=%u) uv=[%.2f..%.2f,%.2f..%.2f] xy=[%d..%d,%d..%d] "
        "tme=%d addr=0x%08X fmt=%d(%s) w=%d h=%d bw=%d swz=%d lvls=%d flt=%d/%d wrap=%d/%d "
        "tfunc=%d(a=%d,dbl=%d) uv_so=(%.3f,%.3f,%.3f,%.3f) "
        "clut=(fmt=%d,sh=%d,m=0x%02X,st=%d,n=%u) "
        "abe=%d(op=%d,s=%d,d=%d,sf=0x%06X,df=0x%06X) ate=%d(f=%d,r=%d,m=0x%02X) "
        "tex_hash=0x%016" PRIX64 " clut_hash=0x%016" PRIX64,
        st.current_frame, list_num, st.draw_index, prim_name ? prim_name : "?", count,
        vtype, through, uv_fmt, col_fmt,
        min_u, max_u, min_v, max_v,
        min_x, max_x, min_y, max_y,
        ds.tex_enable, ds.tex_addr, ds.tex_fmt, TexFmtName(ds.tex_fmt),
        ds.tex_w, ds.tex_h, ds.tex_bw, ds.tex_swizzle, ds.tex_max_level + 1,
        ds.tex_min_filter, ds.tex_mag_filter, ds.tex_clamp_u, ds.tex_clamp_v,
        ds.tex_func, ds.tex_alpha, ds.tex_double,
        xf.uscale, xf.vscale, xf.uoff, xf.voff,
        ds.clut_fmt, ds.clut_shift, ds.clut_mask, ds.clut_start, clut_entries,
        ds.blend, ds.blend_op, ds.blend_src, ds.blend_dst, ds.blend_sfix, ds.blend_dfix,
        ds.atest, ds.atest_func, ds.atest_ref, ds.atest_mask,
        tex_hash, clut_hash);
    if (has_tex_dump)
    {
        std::fprintf(st.log_file, " tex=tex_%04u", tex_id);
    }
    std::fprintf(st.log_file, "\n");
    std::fflush(st.log_file);
    ++st.draw_index;
}

} // namespace repsp::gpu::detail
