// Adapted from sp00nznet/psprecomp (MIT License, commit 5ca18fa, include/psprecomp/render.h).
// Reshaped to repsp types and namespace with no dependency on psprecomp headers.

#ifndef REPSP_GPU_RENDER_BACKEND_H
#define REPSP_GPU_RENDER_BACKEND_H

#include <cstdint>

namespace repsp::gpu
{

// A vertex after format decoding: screen space, colour resolved, texture
// coordinates in texels (through mode addresses texels directly). Colours
// here and below are 0xAABBGGRR, the GE's own byte order.
struct Vertex
{
    int x = 0;
    int y = 0;
    float z = 0.0f;       // depth, 0..65535
    uint32_t rgba = 0;
    float u = 0.0f;
    float v = 0.0f;
    int behind = 0;       // behind the eye: triangles using it are dropped
};

// GE primitive types, from the PRIM argument's type field.
enum PrimitiveType
{
    kPrimPoints = 0,
    kPrimLines,
    kPrimLineStrip,
    kPrimTriangles,
    kPrimTriangleStrip,
    kPrimTriangleFan,
    kPrimSprites
};

// GE texture / CLUT formats (TPSM, CMODE). 0-3 are also the framebuffer and
// display formats.
enum TextureFormat
{
    kTpsm565 = 0,
    kTpsm5551,
    kTpsm4444,
    kTpsm8888,
    kTpsmT4,
    kTpsmT8,
    kTpsmT16,
    kTpsmT32,
    kTpsmDxt1,
    kTpsmDxt3,
    kTpsmDxt5
};

// Everything about the GE's state that changes what a primitive's pixels
// become. Filled by the interpreter from the state commands; read by the
// backend.
struct DrawState
{
    int clear = 0;          // CLEAR bit 0: write vertex colour, nothing else
    int clear_color = 0;    // CLEAR bit 8: ...to RGB
    int clear_alpha = 0;    // CLEAR bit 9: ...to alpha (stencil)
    int clear_depth = 0;    // CLEAR bit 10: ...to depth
    int gouraud = 0;        // SHADE

    int tex_enable = 0;     // TME
    uint32_t tex_addr = 0;  // TBP0 + TBW0 high byte
    int tex_bw = 0;         // TBW0: buffer width, texels
    int tex_w = 1;          // TSIZE0, as sizes not logs
    int tex_h = 1;
    int tex_fmt = 0;        // TPSM
    int tex_swizzle = 0;    // TMODE bit 0
    int tex_clamp_u = 0;    // TWRAP
    int tex_clamp_v = 0;
    int tex_func = 0;       // TFUNC bits 0-2
    int tex_alpha = 1;      // TFUNC bit 8: texture alpha is used
    int tex_double = 0;     // TFUNC bit 16: colour doubling
    uint32_t tex_env = 0;   // TEC, for the blend function

    const uint8_t* clut = nullptr; // bytes captured at the last CLOAD
    int clut_fmt = 0;       // CMODE bits 0-1
    int clut_shift = 0;     // CMODE bits 2-6
    int clut_mask = 0xFF;   // CMODE bits 8-15
    int clut_start = 0;     // CMODE bits 16-20, in units of 16 entries

    int blend = 0;          // ABE
    int blend_src = 0;      // ALPHA
    int blend_dst = 0;
    int blend_op = 0;
    uint32_t blend_sfix = 0; // SFIX
    uint32_t blend_dfix = 0; // DFIX

    int atest = 0;          // ATE
    int atest_func = 1;     // ATST
    int atest_ref = 0;
    int atest_mask = 0xFF;

    int sc_x0 = 0;          // SCISSOR1/2, inclusive
    int sc_y0 = 0;
    int sc_x1 = 479;
    int sc_y1 = 271;

    int ztest = 0;          // ZTE
    int zfunc = 0;          // ZTST: 0 never .. 7 greater-or-equal
    int zwrite = 1;         // !ZMSK
    uint32_t zaddr = 0;     // ZBP/ZBW, in VRAM; 16 bits per pixel
    int zbw = 0;            // ZBW: width in pixels
};

struct RenderBackend
{
    const char* name = nullptr;

    int (*init)(int width, int height) = nullptr;
    void (*shutdown)() = nullptr;

    // GE_FBP / GE_FBW / GE_PSM: the framebuffer being drawn into.
    void (*set_target)(uint32_t addr, uint32_t stride, int fmt) = nullptr;

    // One assembled primitive. `count` vertices, already in screen space,
    // drawn with state `st`.
    void (*draw)(int prim, const Vertex* v, int count, const DrawState* st) = nullptr;

    // End of a display list -- a natural point to flush batched work.
    void (*finish)() = nullptr;

    // sceDisplaySetFrameBuf -- show what has accumulated.
    void (*present)() = nullptr;
};

// Select a backend by name ("software", "null"). Returns 0 on success,
// -1 if the name is unknown, leaving the current backend in place.
int SelectRenderBackend(const char* name);

// The active backend. Never nullptr -- defaults to software.
const RenderBackend* GetRenderBackend();

extern const RenderBackend kSoftwareBackend;
extern const RenderBackend kNullBackend;

uint64_t GetRenderedPixelCount();
void ResetRenderedPixelCount();
uint64_t GetUnsupportedTexelCount();
uint64_t GetOutOfBoundsVramWriteCount();

// One pixel in format 0-3 (565, 5551, 4444, 8888) as 0xAABBGGRR.
uint32_t ColorExpand(uint32_t px, int fmt);

// The texel at integer coordinates (x, y), wrapped or clamped, as 0xAABBGGRR.
uint32_t TexelFetch(const DrawState& st, int x, int y);

} // namespace repsp::gpu

#endif // REPSP_GPU_RENDER_BACKEND_H
