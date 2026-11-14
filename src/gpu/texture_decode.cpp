// Adapted from sp00nznet/psprecomp (MIT License, commit 5ca18fa, src/render.c).
// Reshaped to repsp types and GuestMemory with no dependency on psprecomp headers.

#include "texture_decode.h"
#include "repsp/runtime/guest_memory.h"

namespace repsp::gpu
{

namespace
{

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

inline int ChannelR(uint32_t c) { return static_cast<int>(c & 0xFFu); }
inline int ChannelG(uint32_t c) { return static_cast<int>((c >> 8) & 0xFFu); }
inline int ChannelB(uint32_t c) { return static_cast<int>((c >> 16) & 0xFFu); }

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

} // namespace

int WrapCoord(int c, int size, int clamp)
{
    if (clamp)
    {
        return c < 0 ? 0 : (c >= size ? size - 1 : c);
    }
    return c & (size - 1);
}

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

uint32_t TexelFetchWithCounter(const DrawState& st, int x, int y, uint64_t* bad_texels)
{
    if (st.tex_fmt > kTpsmDxt5 || st.tex_w <= 0 || st.tex_h <= 0 || st.tex_bw <= 0)
    {
        if (bad_texels != nullptr)
        {
            ++(*bad_texels);
        }
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

void DecodeTextureRgba8(const DrawState& st, uint8_t* out_rgba8, uint64_t* bad_texels)
{
    if (out_rgba8 == nullptr || st.tex_w <= 0 || st.tex_h <= 0)
    {
        return;
    }
    for (int y = 0; y < st.tex_h; ++y)
    {
        for (int x = 0; x < st.tex_w; ++x)
        {
            const uint32_t px = TexelFetchWithCounter(st, x, y, bad_texels);
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(st.tex_w) + static_cast<size_t>(x)) * 4u;
            out_rgba8[idx + 0] = static_cast<uint8_t>(px & 0xFFu);
            out_rgba8[idx + 1] = static_cast<uint8_t>((px >> 8) & 0xFFu);
            out_rgba8[idx + 2] = static_cast<uint8_t>((px >> 16) & 0xFFu);
            out_rgba8[idx + 3] = static_cast<uint8_t>((px >> 24) & 0xFFu);
        }
    }
}

uint32_t ComputeTextureDataSizeBytes(const DrawState& st)
{
    if (st.tex_fmt < 0 || st.tex_fmt > kTpsmDxt5 || st.tex_w <= 0 || st.tex_h <= 0 || st.tex_bw <= 0)
    {
        return 0;
    }
    if (st.tex_fmt >= kTpsmDxt1)
    {
        const uint32_t blk_size = (st.tex_fmt == kTpsmDxt1) ? 8u : 16u;
        const uint32_t bw_blocks = static_cast<uint32_t>(st.tex_bw) / 4u;
        const uint32_t h_blocks = (static_cast<uint32_t>(st.tex_h) + 3u) / 4u;
        return bw_blocks * h_blocks * blk_size;
    }
    const uint32_t rowb = static_cast<uint32_t>(st.tex_bw) * static_cast<uint32_t>(kTexBpp[st.tex_fmt]) / 8u;
    const uint32_t h = st.tex_swizzle ? ((static_cast<uint32_t>(st.tex_h) + 7u) & ~7u) : static_cast<uint32_t>(st.tex_h);
    return rowb * h;
}

} // namespace repsp::gpu
