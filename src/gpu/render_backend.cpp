// Adapted from sp00nznet/psprecomp (MIT License, commit 5ca18fa, src/render.c).
// Reshaped to repsp types and GuestMemory with no dependency on psprecomp headers.

#include "repsp/gpu/render_backend.h"
#include "repsp/runtime/guest_memory.h"

#include <cstring>

namespace repsp::gpu
{

namespace
{

uint32_t g_fb_addr = 0;
uint32_t g_fb_stride = 0;
int g_fb_fmt = 3;
uint64_t g_pixels = 0;
uint64_t g_bad_texels = 0;
uint64_t g_oob_vram_writes = 0;

inline bool IsInVram(uint32_t addr, uint32_t size)
{
    if (addr < runtime::GuestMemory::kVramAddress)
    {
        return false;
    }
    const uint32_t off = addr - runtime::GuestMemory::kVramAddress;
    return off <= runtime::GuestMemory::kVramSize &&
           size <= (runtime::GuestMemory::kVramSize - off);
}

inline uint8_t Read8(uint32_t addr)
{
    return runtime::GetGuestMemory().ReadU8(addr);
}

inline uint16_t Read16(uint32_t addr)
{
    return runtime::GetGuestMemory().ReadU16(addr);
}

inline uint32_t Read32(uint32_t addr)
{
    return runtime::GetGuestMemory().ReadU32(addr);
}

inline void Write16(uint32_t addr, uint16_t val)
{
    runtime::GetGuestMemory().WriteU16(addr, val);
}

inline void Write32(uint32_t addr, uint32_t val)
{
    runtime::GetGuestMemory().WriteU32(addr, val);
}

inline int ChannelR(uint32_t c) { return static_cast<int>(c & 0xFFu); }
inline int ChannelG(uint32_t c) { return static_cast<int>((c >> 8) & 0xFFu); }
inline int ChannelB(uint32_t c) { return static_cast<int>((c >> 16) & 0xFFu); }
inline int ChannelA(uint32_t c) { return static_cast<int>((c >> 24) & 0xFFu); }

uint32_t PackRgba(int r, int g, int b, int a)
{
    if (r > 255) r = 255; else if (r < 0) r = 0;
    if (g > 255) g = 255; else if (g < 0) g = 0;
    if (b > 255) b = 255; else if (b < 0) b = 0;
    if (a > 255) a = 255; else if (a < 0) a = 0;
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

inline int Mul8(int a, int b)
{
    return (a * b + 127) / 255;
}

uint32_t ColorPack(uint32_t c, int fmt)
{
    switch (fmt)
    {
    case 0:
        return static_cast<uint32_t>((ChannelR(c) >> 3) | ((ChannelG(c) >> 2) << 5) | ((ChannelB(c) >> 3) << 11));
    case 1:
        return static_cast<uint32_t>((ChannelR(c) >> 3) | ((ChannelG(c) >> 3) << 5) | ((ChannelB(c) >> 3) << 10) | ((ChannelA(c) >> 7) << 15));
    case 2:
        return static_cast<uint32_t>((ChannelR(c) >> 4) | ((ChannelG(c) >> 4) << 4) | ((ChannelB(c) >> 4) << 8) | ((ChannelA(c) >> 4) << 12));
    default:
        return c;
    }
}

constexpr int kTexBpp[11] = { 16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8 };

uint32_t TexOffset(const DrawState& st, uint32_t xb, uint32_t y)
{
    const uint32_t rowb = static_cast<uint32_t>(st.tex_bw) * static_cast<uint32_t>(kTexBpp[st.tex_fmt]) / 8u;
    if (!st.tex_swizzle)
    {
        return y * rowb + xb;
    }
    const uint32_t blocks = rowb / 16u;
    return ((y >> 3) * blocks + (xb >> 4)) * 128u + (y & 7u) * 16u + (xb & 15u);
}

uint32_t ClutEntry(const DrawState& st, uint32_t idx)
{
    const uint32_t e = ((idx >> st.clut_shift) & static_cast<uint32_t>(st.clut_mask)) |
                       (static_cast<uint32_t>(st.clut_start) << 4);
    if (!st.clut)
    {
        return 0;
    }
    if (st.clut_fmt == 3)
    {
        if (e >= 256)
        {
            return 0;
        }
        const uint8_t* p = st.clut + e * 4u;
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }
    if (e >= 512)
    {
        return 0;
    }
    const uint8_t* p = st.clut + e * 2u;
    return ColorExpand(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8), st.clut_fmt);
}

int WrapCoord(int c, int size, int clamp)
{
    if (clamp)
    {
        return c < 0 ? 0 : (c >= size ? size - 1 : c);
    }
    return c & (size - 1);
}

uint32_t DxtTexel(const DrawState& st, uint32_t x, uint32_t y)
{
    const uint32_t size = (st.tex_fmt == kTpsmDxt1) ? 8u : 16u;
    const uint32_t blk = st.tex_addr + ((y >> 2) * (static_cast<uint32_t>(st.tex_bw) / 4u) + (x >> 2)) * size;
    const uint32_t i = (y & 3u) * 4u + (x & 3u);

    const uint32_t c0 = Read16(blk + 4u);
    const uint32_t c1 = Read16(blk + 6u);
    const uint32_t a = ColorExpand(c0, 0);
    const uint32_t b = ColorExpand(c1, 0);
    uint32_t out = 0;
    switch ((Read32(blk) >> (2u * i)) & 3u)
    {
    case 0:
        out = a;
        break;
    case 1:
        out = b;
        break;
    case 2:
        out = (c0 > c1 || st.tex_fmt != kTpsmDxt1)
            ? PackRgba((2 * ChannelR(a) + ChannelR(b)) / 3, (2 * ChannelG(a) + ChannelG(b)) / 3, (2 * ChannelB(a) + ChannelB(b)) / 3, 255)
            : PackRgba((ChannelR(a) + ChannelR(b)) / 2, (ChannelG(a) + ChannelG(b)) / 2, (ChannelB(a) + ChannelB(b)) / 2, 255);
        break;
    default:
        out = (c0 > c1 || st.tex_fmt != kTpsmDxt1)
            ? PackRgba((ChannelR(a) + 2 * ChannelR(b)) / 3, (ChannelG(a) + 2 * ChannelG(b)) / 3, (ChannelB(a) + 2 * ChannelB(b)) / 3, 255)
            : 0;
        break;
    }

    if (st.tex_fmt == kTpsmDxt3)
    {
        const uint32_t al = (Read16(blk + 8u + (y & 3u) * 2u) >> ((x & 3u) * 4u)) & 0xFu;
        out = (out & 0x00FFFFFFu) | ((al * 17u) << 24);
    }
    else if (st.tex_fmt == kTpsmDxt5)
    {
        const uint64_t bits = static_cast<uint64_t>(Read16(blk + 12u)) | (static_cast<uint64_t>(Read32(blk + 8u)) << 16);
        const int a0 = Read8(blk + 14u);
        const int a1 = Read8(blk + 15u);
        const int k = static_cast<int>(bits >> (3u * i)) & 7;
        int al = 0;
        if (k == 0) al = a0;
        else if (k == 1) al = a1;
        else if (a0 > a1) al = ((8 - k) * a0 + (k - 1) * a1) / 7;
        else if (k < 6) al = ((6 - k) * a0 + (k - 1) * a1) / 5;
        else al = (k == 6) ? 0 : 255;
        out = (out & 0x00FFFFFFu) | (static_cast<uint32_t>(al) << 24);
    }
    return out;
}

uint32_t TexCombine(const DrawState& st, uint32_t t, uint32_t c)
{
    int r = 0, g = 0, b = 0, a = 0;
    const int ta = st.tex_alpha;
    switch (st.tex_func)
    {
    case 0: // modulate
        r = Mul8(ChannelR(t), ChannelR(c));
        g = Mul8(ChannelG(t), ChannelG(c));
        b = Mul8(ChannelB(t), ChannelB(c));
        a = ta ? Mul8(ChannelA(t), ChannelA(c)) : ChannelA(c);
        break;
    case 1: // decal
        if (ta)
        {
            r = (ChannelR(t) * ChannelA(t) + ChannelR(c) * (255 - ChannelA(t)) + 127) / 255;
            g = (ChannelG(t) * ChannelA(t) + ChannelG(c) * (255 - ChannelA(t)) + 127) / 255;
            b = (ChannelB(t) * ChannelA(t) + ChannelB(c) * (255 - ChannelA(t)) + 127) / 255;
        }
        else
        {
            r = ChannelR(t); g = ChannelG(t); b = ChannelB(t);
        }
        a = ChannelA(c);
        break;
    case 2: // blend
    {
        const uint32_t e = st.tex_env;
        r = (ChannelR(c) * (255 - ChannelR(t)) + ChannelR(e) * ChannelR(t) + 127) / 255;
        g = (ChannelG(c) * (255 - ChannelG(t)) + ChannelG(e) * ChannelG(t) + 127) / 255;
        b = (ChannelB(c) * (255 - ChannelB(t)) + ChannelB(e) * ChannelB(t) + 127) / 255;
        a = ta ? Mul8(ChannelA(t), ChannelA(c)) : ChannelA(c);
        break;
    }
    case 3: // replace
        r = ChannelR(t); g = ChannelG(t); b = ChannelB(t);
        a = ta ? ChannelA(t) : ChannelA(c);
        break;
    default: // 4: add
        r = ChannelR(t) + ChannelR(c);
        g = ChannelG(t) + ChannelG(c);
        b = ChannelB(t) + ChannelB(c);
        a = ta ? Mul8(ChannelA(t), ChannelA(c)) : ChannelA(c);
        break;
    }
    if (st.tex_double)
    {
        r *= 2; g *= 2; b *= 2;
    }
    return PackRgba(r, g, b, a);
}

bool AlphaPass(const DrawState& st, uint32_t c)
{
    const int a = ChannelA(c) & st.atest_mask;
    const int ref = st.atest_ref & st.atest_mask;
    switch (st.atest_func)
    {
    case 0: return false;
    case 1: return true;
    case 2: return a == ref;
    case 3: return a != ref;
    case 4: return a < ref;
    case 5: return a <= ref;
    case 6: return a > ref;
    default: return a >= ref;
    }
}

bool ColorPass(const DrawState& st, uint32_t c)
{
    const uint32_t masked_c = (c & 0x00FFFFFFu) & st.ctest_mask;
    const uint32_t masked_ref = st.ctest_ref & st.ctest_mask;
    switch (st.ctest_func & 3)
    {
    case 0: return false;
    case 1: return true;
    case 2: return masked_c == masked_ref;
    default: return masked_c != masked_ref;
    }
}

int BlendFactor(int code, bool is_src, int ch, uint32_t s, uint32_t d, uint32_t fix)
{
    const int other = is_src ? static_cast<int>((d >> (ch * 8)) & 0xFFu)
                             : static_cast<int>((s >> (ch * 8)) & 0xFFu);
    switch (code)
    {
    case 0: return other;
    case 1: return 255 - other;
    case 2: return ChannelA(s);
    case 3: return 255 - ChannelA(s);
    case 4: return ChannelA(d);
    case 5: return 255 - ChannelA(d);
    case 6: return (2 * ChannelA(s) > 255) ? 255 : 2 * ChannelA(s);
    case 7: return (255 - 2 * ChannelA(s) < 0) ? 0 : 255 - 2 * ChannelA(s);
    case 8: return (2 * ChannelA(d) > 255) ? 255 : 2 * ChannelA(d);
    case 9: return (255 - 2 * ChannelA(d) < 0) ? 0 : 255 - 2 * ChannelA(d);
    default: return static_cast<int>((fix >> (ch * 8)) & 0xFFu);
    }
}

uint32_t Blend(const DrawState& st, uint32_t s, uint32_t d)
{
    int out[3] = {0, 0, 0};
    for (int ch = 0; ch < 3; ch++)
    {
        const int sc = static_cast<int>((s >> (ch * 8)) & 0xFFu);
        const int dc = static_cast<int>((d >> (ch * 8)) & 0xFFu);
        const int sv = Mul8(sc, BlendFactor(st.blend_src, true, ch, s, d, st.blend_sfix));
        const int dv = Mul8(dc, BlendFactor(st.blend_dst, false, ch, s, d, st.blend_dfix));
        switch (st.blend_op)
        {
        case 0: out[ch] = sv + dv; break;
        case 1: out[ch] = sv - dv; break;
        case 2: out[ch] = dv - sv; break;
        case 3: out[ch] = sc < dc ? sc : dc; break;
        case 4: out[ch] = sc > dc ? sc : dc; break;
        default: out[ch] = sc > dc ? sc - dc : dc - sc; break;
        }
    }
    return PackRgba(out[0], out[1], out[2], ChannelA(s));
}

int SwInit(int w, int h) { (void)w; (void)h; return 0; }
void SwShutdown() {}

void SwTarget(uint32_t addr, uint32_t stride, int fmt)
{
    g_fb_addr = addr;
    g_fb_stride = stride;
    g_fb_fmt = fmt;
}

uint32_t FbRead(uint32_t at)
{
    return (g_fb_fmt == 3) ? Read32(at) : ColorExpand(Read16(at), g_fb_fmt);
}

bool DepthPass(int func, int z, int d)
{
    switch (func)
    {
    case 0: return false;
    case 1: return true;
    case 2: return z == d;
    case 3: return z != d;
    case 4: return z < d;
    case 5: return z <= d;
    case 6: return z > d;
    default: return z >= d;
    }
}

void Fragment(const DrawState& st, int x, int y, uint32_t c, float u, float v, float zf)
{
    if (!g_fb_addr || !g_fb_stride) return;
    if (x < 0 || y < 0 || x >= 480 || y >= 272) return;
    if (x < st.sc_x0 || x > st.sc_x1 || y < st.sc_y0 || y > st.sc_y1) return;

    const uint32_t bytes = (g_fb_fmt == 3) ? 4u : 2u;
    const uint32_t at = g_fb_addr + static_cast<uint32_t>(y * static_cast<int>(g_fb_stride) + x) * bytes;
    if (!IsInVram(at, bytes))
    {
        ++g_oob_vram_writes;
        return;
    }
    const int z = zf < 0.0f ? 0 : (zf > 65535.0f ? 65535 : static_cast<int>(zf + 0.5f));
    const uint32_t zat = st.zaddr + static_cast<uint32_t>(y * st.zbw + x) * 2u;
    const bool have_z = (st.zaddr != 0 && st.zbw > 0);
    const bool z_in_vram = have_z && IsInVram(zat, 2u);

    if (st.clear)
    {
        if (st.clear_depth && have_z)
        {
            if (z_in_vram)
            {
                Write16(zat, static_cast<uint16_t>(z));
            }
            else
            {
                ++g_oob_vram_writes;
            }
        }
        if (!st.clear_color && !st.clear_alpha)
        {
            return;
        }
        if (!st.clear_color || !st.clear_alpha)
        {
            const uint32_t d = FbRead(at);
            c = st.clear_color ? ((c & 0x00FFFFFFu) | (d & 0xFF000000u))
                               : ((d & 0x00FFFFFFu) | (c & 0xFF000000u));
        }
    }
    else
    {
        if (st.tex_enable)
        {
            int tx = static_cast<int>(u);
            int ty = static_cast<int>(v);
            if (u < 0.0f && static_cast<float>(tx) != u) --tx;
            if (v < 0.0f && static_cast<float>(ty) != v) --ty;
            c = TexCombine(st, TexelFetch(st, tx, ty), c);
        }
        if (st.ctest && !ColorPass(st, c)) return;
        if (st.atest && !AlphaPass(st, c)) return;
        if (st.ztest && have_z)
        {
            if (!z_in_vram)
            {
                ++g_oob_vram_writes;
                return;
            }
            if (!DepthPass(st.zfunc, z, Read16(zat))) return;
            if (st.zwrite) Write16(zat, static_cast<uint16_t>(z));
        }
        if (st.blend)
        {
            c = Blend(st, c, FbRead(at));
        }
    }

    if (bytes == 4u)
    {
        Write32(at, c);
    }
    else
    {
        Write16(at, static_cast<uint16_t>(ColorPack(c, g_fb_fmt)));
    }
    ++g_pixels;
}

int Edge(const Vertex& p, const Vertex& q, int x, int y)
{
    return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
}

bool IsTopLeftEdge(const Vertex& p, const Vertex& q)
{
    return (q.y < p.y) || (q.y == p.y && q.x > p.x);
}

void SwTri(const Vertex& a_in, const Vertex& b_in, const Vertex& c_in, const DrawState& st)
{
    if (a_in.behind || b_in.behind || c_in.behind) return;

    Vertex a = a_in;
    Vertex b = b_in;
    Vertex c = c_in;
    int area = Edge(a, b, c.x, c.y);
    if (area == 0) return;
    if (area < 0)
    {
        const Vertex tmp = b;
        b = c;
        c = tmp;
        area = -area;
    }

    int minx = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
    int maxx = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
    int miny = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
    int maxy = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > 479) maxx = 479;
    if (maxy > 271) maxy = 271;

    // Evaluate edge functions at pixel centers (x + 0.5, y + 0.5) scaled by 2
    // with top-left fill rule tie-breaking so shared edges are drawn once and
    // bottom/right quad boundaries do not over-sample padding texels.
    const float inv2 = 1.0f / static_cast<float>(2 * area);
    const int bias0 = IsTopLeftEdge(a, b) ? 0 : -1;
    const int bias1 = IsTopLeftEdge(b, c) ? 0 : -1;
    const int bias2 = IsTopLeftEdge(c, a) ? 0 : -1;
    const int c0 = (b.x - a.x) - (b.y - a.y);
    const int c1 = (c.x - b.x) - (c.y - b.y);
    const int c2 = (a.x - c.x) - (a.y - c.y);

    for (int y = miny; y <= maxy; ++y)
    {
        for (int x = minx; x <= maxx; ++x)
        {
            const int e0 = 2 * Edge(a, b, x, y) + c0;
            const int e1 = 2 * Edge(b, c, x, y) + c1;
            const int e2 = 2 * Edge(c, a, x, y) + c2;
            if (e0 + bias0 < 0 || e1 + bias1 < 0 || e2 + bias2 < 0)
            {
                continue;
            }
            const float la = static_cast<float>(e1) * inv2;
            const float lb = static_cast<float>(e2) * inv2;
            const float lc = static_cast<float>(e0) * inv2;
            uint32_t col = c.rgba;
            if (st.gouraud)
            {
                col = PackRgba(
                    static_cast<int>(ChannelR(a.rgba) * la + ChannelR(b.rgba) * lb + ChannelR(c.rgba) * lc + 0.5f),
                    static_cast<int>(ChannelG(a.rgba) * la + ChannelG(b.rgba) * lb + ChannelG(c.rgba) * lc + 0.5f),
                    static_cast<int>(ChannelB(a.rgba) * la + ChannelB(b.rgba) * lb + ChannelB(c.rgba) * lc + 0.5f),
                    static_cast<int>(ChannelA(a.rgba) * la + ChannelA(b.rgba) * lb + ChannelA(c.rgba) * lc + 0.5f));
            }
            Fragment(st, x, y, col,
                     a.u * la + b.u * lb + c.u * lc,
                     a.v * la + b.v * lb + c.v * lc,
                     a.z * la + b.z * lb + c.z * lc);
        }
    }
}

void SwSprite(const Vertex& a, const Vertex& b, const DrawState& st)
{
    const int x0 = a.x < b.x ? a.x : b.x;
    const int x1 = a.x > b.x ? a.x : b.x;
    const int y0 = a.y < b.y ? a.y : b.y;
    const int y1 = a.y > b.y ? a.y : b.y;
    if (x0 == x1 || y0 == y1) return;

    const float du = (b.u - a.u) / static_cast<float>(b.x - a.x);
    const float dv = (b.v - a.v) / static_cast<float>(b.y - a.y);
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            Fragment(st, x, y, b.rgba,
                     a.u + (static_cast<float>(x - a.x) + 0.5f) * du,
                     a.v + (static_cast<float>(y - a.y) + 0.5f) * dv,
                     b.z);
        }
    }
}

void SwDraw(int prim, const Vertex* v, int count, const DrawState* st)
{
    if (!v || !st) return;
    switch (prim)
    {
    case kPrimSprites:
        for (int i = 0; i + 1 < count; i += 2) SwSprite(v[i], v[i + 1], *st);
        break;
    case kPrimTriangles:
        for (int i = 0; i + 2 < count; i += 3) SwTri(v[i], v[i + 1], v[i + 2], *st);
        break;
    case kPrimTriangleStrip:
        for (int i = 0; i + 2 < count; ++i) SwTri(v[i], v[i + 1], v[i + 2], *st);
        break;
    case kPrimTriangleFan:
        for (int i = 1; i + 1 < count; ++i) SwTri(v[0], v[i], v[i + 1], *st);
        break;
    default:
        break;
    }
}

void Noop() {}
int NullInit(int w, int h) { (void)w; (void)h; return 0; }
void NullTarget(uint32_t a, uint32_t s, int f) { (void)a; (void)s; (void)f; }
void NullDraw(int p, const Vertex* v, int n, const DrawState* st) { (void)p; (void)v; (void)n; (void)st; }

const RenderBackend* g_backend = &kSoftwareBackend;

} // namespace

const RenderBackend kSoftwareBackend = {
    "software", SwInit, SwShutdown, SwTarget, SwDraw, Noop, Noop
};

const RenderBackend kNullBackend = {
    "null", NullInit, Noop, NullTarget, NullDraw, Noop, Noop
};

int SelectRenderBackend(const char* name)
{
    static const RenderBackend* const kAll[] = { &kSoftwareBackend, &kNullBackend };
    if (!name) return -1;
    for (const RenderBackend* b : kAll)
    {
        if (std::strcmp(b->name, name) == 0)
        {
            g_backend = b;
            return 0;
        }
    }
    return -1;
}

const RenderBackend* GetRenderBackend()
{
    return g_backend;
}

uint64_t GetRenderedPixelCount() { return g_pixels; }
void ResetRenderedPixelCount() { g_pixels = 0; g_bad_texels = 0; g_oob_vram_writes = 0; }
uint64_t GetUnsupportedTexelCount() { return g_bad_texels; }
uint64_t GetOutOfBoundsVramWriteCount() { return g_oob_vram_writes; }

uint32_t ColorExpand(uint32_t px, int fmt)
{
    switch (fmt)
    {
    case 0: // 565
    {
        const uint32_t r = px & 0x1Fu;
        const uint32_t g = (px >> 5) & 0x3Fu;
        const uint32_t b = (px >> 11) & 0x1Fu;
        return PackRgba(static_cast<int>((r << 3) | (r >> 2)),
                        static_cast<int>((g << 2) | (g >> 4)),
                        static_cast<int>((b << 3) | (b >> 2)),
                        255);
    }
    case 1: // 5551
    {
        const uint32_t r = px & 0x1Fu;
        const uint32_t g = (px >> 5) & 0x1Fu;
        const uint32_t b = (px >> 10) & 0x1Fu;
        return PackRgba(static_cast<int>((r << 3) | (r >> 2)),
                        static_cast<int>((g << 3) | (g >> 2)),
                        static_cast<int>((b << 3) | (b >> 2)),
                        (px & 0x8000u) ? 255 : 0);
    }
    case 2: // 4444
    {
        const uint32_t r = px & 0xFu;
        const uint32_t g = (px >> 4) & 0xFu;
        const uint32_t b = (px >> 8) & 0xFu;
        const uint32_t a = (px >> 12) & 0xFu;
        return PackRgba(static_cast<int>((r << 4) | r),
                        static_cast<int>((g << 4) | g),
                        static_cast<int>((b << 4) | b),
                        static_cast<int>((a << 4) | a));
    }
    default: // 8888
        return px;
    }
}

uint32_t TexelFetch(const DrawState& st, int x, int y)
{
    if (st.tex_fmt > kTpsmDxt5 || st.tex_w <= 0 || st.tex_h <= 0 || st.tex_bw <= 0)
    {
        ++g_bad_texels;
        return 0xFFFF00FFu; // loud magenta
    }
    x = WrapCoord(x, st.tex_w, st.tex_clamp_u);
    y = WrapCoord(y, st.tex_h, st.tex_clamp_v);
    if (st.tex_fmt >= kTpsmDxt1)
    {
        return DxtTexel(st, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    }

    const int bpp = kTexBpp[st.tex_fmt];
    const uint32_t at = st.tex_addr + TexOffset(st, static_cast<uint32_t>(x) * static_cast<uint32_t>(bpp) / 8u, static_cast<uint32_t>(y));
    switch (st.tex_fmt)
    {
    case kTpsm565:
    case kTpsm5551:
    case kTpsm4444:
        return ColorExpand(Read16(at), st.tex_fmt);
    case kTpsm8888:
        return Read32(at);
    case kTpsmT4:
        return ClutEntry(st, (Read8(at) >> ((x & 1) * 4)) & 0xFu);
    case kTpsmT8:
        return ClutEntry(st, Read8(at));
    case kTpsmT16:
        return ClutEntry(st, Read16(at));
    default: // T32
        return ClutEntry(st, Read32(at));
    }
}

} // namespace repsp::gpu
