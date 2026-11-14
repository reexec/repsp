// Adapted from sp00nznet/psprecomp (MIT License, commit 5ca18fa, src/hle/ge.c).
// Reshaped to repsp types and GuestMemory with no dependency on psprecomp headers.

#include "repsp/gpu/ge_interpreter.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/runtime/guest_memory.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace repsp::gpu
{

namespace
{

constexpr uint32_t kGeNop = 0x00;
constexpr uint32_t kGeVaddr = 0x01;
constexpr uint32_t kGeIaddr = 0x02;
constexpr uint32_t kGePrim = 0x04;
constexpr uint32_t kGeBezier = 0x05;
constexpr uint32_t kGeSpline = 0x06;
constexpr uint32_t kGeJump = 0x08;
constexpr uint32_t kGeBjump = 0x09;
constexpr uint32_t kGeCall = 0x0A;
constexpr uint32_t kGeRet = 0x0B;
constexpr uint32_t kGeEnd = 0x0C;
constexpr uint32_t kGeSignal = 0x0E;
constexpr uint32_t kGeFinish = 0x0F;
constexpr uint32_t kGeBase = 0x10;
constexpr uint32_t kGeVtype = 0x12;
constexpr uint32_t kGeOffsetAddr = 0x13;
constexpr uint32_t kGeOriginAddr = 0x14;
constexpr uint32_t kGeLte = 0x17;
constexpr uint32_t kGeTme = 0x1E;
constexpr uint32_t kGeAbe = 0x21;
constexpr uint32_t kGeAte = 0x22;
constexpr uint32_t kGeZte = 0x23;
constexpr uint32_t kGeCte = 0x27;
constexpr uint32_t kGeWms = 0x3A;
constexpr uint32_t kGeWorld = 0x3B;
constexpr uint32_t kGeVms = 0x3C;
constexpr uint32_t kGeView = 0x3D;
constexpr uint32_t kGePms = 0x3E;
constexpr uint32_t kGeProj = 0x3F;
constexpr uint32_t kGeXscale = 0x42;
constexpr uint32_t kGeYscale = 0x43;
constexpr uint32_t kGeZscale = 0x44;
constexpr uint32_t kGeXpos = 0x45;
constexpr uint32_t kGeYpos = 0x46;
constexpr uint32_t kGeZpos = 0x47;
constexpr uint32_t kGeUscale = 0x48;
constexpr uint32_t kGeVscale = 0x49;
constexpr uint32_t kGeUoffset = 0x4A;
constexpr uint32_t kGeVoffset = 0x4B;
constexpr uint32_t kGeOffsetX = 0x4C;
constexpr uint32_t kGeOffsetY = 0x4D;
constexpr uint32_t kGeShade = 0x50;
constexpr uint32_t kGeRnorm = 0x51;
constexpr uint32_t kGeCmat = 0x53;
constexpr uint32_t kGeEmc = 0x54;
constexpr uint32_t kGeAmc = 0x55;
constexpr uint32_t kGeDmc = 0x56;
constexpr uint32_t kGeSmc = 0x57;
constexpr uint32_t kGeAma = 0x58;
constexpr uint32_t kGeSpow = 0x5B;
constexpr uint32_t kGeAlc = 0x5C;
constexpr uint32_t kGeAla = 0x5D;
constexpr uint32_t kGeFbp = 0x9C;
constexpr uint32_t kGeFbw = 0x9D;
constexpr uint32_t kGeZbp = 0x9E;
constexpr uint32_t kGeZbw = 0x9F;
constexpr uint32_t kGeTbp0 = 0xA0;
constexpr uint32_t kGeTbw0 = 0xA8;
constexpr uint32_t kGeCbp = 0xB0;
constexpr uint32_t kGeCbph = 0xB1;
constexpr uint32_t kGeTsize0 = 0xB8;
constexpr uint32_t kGeTmode = 0xC2;
constexpr uint32_t kGeTpsm = 0xC3;
constexpr uint32_t kGeCload = 0xC4;
constexpr uint32_t kGeCmode = 0xC5;
constexpr uint32_t kGeTwrap = 0xC7;
constexpr uint32_t kGeTfunc = 0xC9;
constexpr uint32_t kGeTec = 0xCA;
constexpr uint32_t kGePsm = 0xD2;
constexpr uint32_t kGeClear = 0xD3;
constexpr uint32_t kGeScissor1 = 0xD4;
constexpr uint32_t kGeScissor2 = 0xD5;
constexpr uint32_t kGeCtest = 0xD8;
constexpr uint32_t kGeCref = 0xD9;
constexpr uint32_t kGeCmsk = 0xDA;
constexpr uint32_t kGeAtst = 0xDB;
constexpr uint32_t kGeZtst = 0xDE;
constexpr uint32_t kGeAlpha = 0xDF;
constexpr uint32_t kGeSfix = 0xE0;
constexpr uint32_t kGeDfix = 0xE1;
constexpr uint32_t kGeZmsk = 0xE7;

constexpr int32_t kErrNoMemory = static_cast<int32_t>(0x800200D9u);
constexpr int32_t kErrUnknownUid = static_cast<int32_t>(0x800200CBu);

const char* const kPrimNames[8] = {
    "points", "lines", "line-strip", "triangles",
    "triangle-strip", "triangle-fan", "sprites", "?"
};

inline uint8_t Read8(uint32_t addr) { return runtime::GetGuestMemory().ReadU8(addr); }
inline uint16_t Read16(uint32_t addr) { return runtime::GetGuestMemory().ReadU16(addr); }
inline uint32_t Read32(uint32_t addr) { return runtime::GetGuestMemory().ReadU32(addr); }
inline float ReadF32(uint32_t addr)
{
    uint32_t raw = Read32(addr);
    float f = 0.0f;
    std::memcpy(&f, &raw, sizeof(f));
    return f;
}

float F24(uint32_t arg)
{
    uint32_t u = arg << 8;
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

void UnpackColor(uint32_t c, float out[4])
{
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<float>((c >> (8 * i)) & 0xFFu);
}

float Norm3(float v[3])
{
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0f)
    {
        for (int i = 0; i < 3; ++i) v[i] /= l;
    }
    return l;
}

inline uint32_t VtTex(uint32_t v) { return v & 3u; }
inline uint32_t VtColor(uint32_t v) { return (v >> 2) & 7u; }
inline uint32_t VtNormal(uint32_t v) { return (v >> 5) & 3u; }
inline uint32_t VtPos(uint32_t v) { return (v >> 7) & 3u; }
inline uint32_t VtWeight(uint32_t v) { return (v >> 9) & 3u; }
inline uint32_t VtIndex(uint32_t v) { return (v >> 11) & 3u; }
inline uint32_t VtThrough(uint32_t v) { return (v >> 23) & 1u; }

int VertexLayout(uint32_t vtype, int* tex_off, int* col_off, int* nrm_off, int* pos_off)
{
    static constexpr int kTexSz[4] = { 0, 1, 2, 4 };
    static constexpr int kColSz[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
    static constexpr int kNormSz[4] = { 0, 1, 2, 4 };
    static constexpr int kPosSz[4] = { 0, 1, 2, 4 };

    int off = 0, align = 1;
    const int t = kTexSz[VtTex(vtype)] * 2;
    const int c = kColSz[VtColor(vtype)];
    const int n = kNormSz[VtNormal(vtype)] * 3;
    const int p = kPosSz[VtPos(vtype)] * 3;

    if (VtWeight(vtype)) return 0;

    const int ts = kTexSz[VtTex(vtype)];
    if (ts) { off = (off + ts - 1) & ~(ts - 1); *tex_off = off; off += t; if (ts > align) align = ts; }
    else *tex_off = -1;

    const int cs = kColSz[VtColor(vtype)];
    if (cs) { off = (off + cs - 1) & ~(cs - 1); *col_off = off; off += c; if (cs > align) align = cs; }
    else *col_off = -1;

    const int ns = kNormSz[VtNormal(vtype)];
    if (ns) { off = (off + ns - 1) & ~(ns - 1); *nrm_off = off; off += n; if (ns > align) align = ns; }
    else *nrm_off = -1;

    const int ps = kPosSz[VtPos(vtype)];
    if (!ps) return 0;
    off = (off + ps - 1) & ~(ps - 1);
    *pos_off = off;
    off += p;
    if (ps > align) align = ps;

    return (off + align - 1) & ~(align - 1);
}

void Read3(uint32_t a, int fmt, int raw, float v[3])
{
    for (int i = 0; i < 3; ++i)
    {
        switch (fmt)
        {
        case 1:
            v[i] = static_cast<float>(static_cast<int8_t>(Read8(a + static_cast<uint32_t>(i))));
            if (!raw) v[i] /= 128.0f;
            break;
        case 2:
            v[i] = static_cast<float>(static_cast<int16_t>(Read16(a + static_cast<uint32_t>(i) * 2u)));
            if (!raw) v[i] /= 32768.0f;
            break;
        default:
            v[i] = ReadF32(a + static_cast<uint32_t>(i) * 4u);
            break;
        }
    }
}

uint32_t FbVramAddr(uint32_t fbp)
{
    return runtime::GuestMemory::kVramAddress | (fbp & (runtime::GuestMemory::kVramSize - 1u));
}

inline uint32_t ResolveGeAddr(uint32_t base, uint32_t offset, uint32_t arg)
{
    return (base | (arg & 0x00FFFFFFu)) + offset;
}

} // namespace

bool GeInterpreter::TransformPoint(const float p[3], float* sx, float* sy, float* sz) const
{
    const float* W = xf_.world;
    const float* V = xf_.view;
    const float* P = xf_.proj;
    float w[3], v[3], c[4];
    for (int r = 0; r < 3; ++r)
        w[r] = W[r] * p[0] + W[3 + r] * p[1] + W[6 + r] * p[2] + W[9 + r];
    for (int r = 0; r < 3; ++r)
        v[r] = V[r] * w[0] + V[3 + r] * w[1] + V[6 + r] * w[2] + V[9 + r];
    for (int r = 0; r < 4; ++r)
        c[r] = P[r] * v[0] + P[4 + r] * v[1] + P[8 + r] * v[2] + P[12 + r];
    if (c[3] <= 0.0f) return false;
    *sx = c[0] / c[3] * xf_.xscale + xf_.xpos - xf_.offx;
    *sy = c[1] / c[3] * xf_.yscale + xf_.ypos - xf_.offy;
    *sz = c[2] / c[3] * xf_.zscale + xf_.zpos;
    return true;
}

uint32_t GeInterpreter::LightVertex(const float p[3], const float n[3], uint32_t vcol) const
{
    const float* W = xf_.world;
    float wp[3], wn[3];
    for (int r = 0; r < 3; ++r)
    {
        wp[r] = W[r] * p[0] + W[3 + r] * p[1] + W[6 + r] * p[2] + W[9 + r];
        wn[r] = W[r] * n[0] + W[3 + r] * n[1] + W[6 + r] * n[2];
    }
    if (lt_.rnorm)
    {
        for (int r = 0; r < 3; ++r) wn[r] = -wn[r];
    }
    Norm3(wn);

    float ma[4], md[4], ms[4], e[4], al[4], out[4];
    UnpackColor((lt_.cmat & 1) ? vcol : material_, ma);
    UnpackColor((lt_.cmat & 2) ? vcol : lt_.diffuse, md);
    UnpackColor((lt_.cmat & 4) ? vcol : lt_.specular, ms);
    UnpackColor(lt_.emissive, e);
    UnpackColor(lt_.ambient, al);
    for (int k = 0; k < 3; ++k) out[k] = e[k] + al[k] * ma[k] / 255.0f;

    for (int i = 0; i < 4; ++i)
    {
        if (!lt_.on[i]) continue;
        float L[3], att = 1.0f;
        if (lt_.type[i] == 0)
        {
            for (int r = 0; r < 3; ++r) L[r] = lt_.pos[i][r];
            Norm3(L);
        }
        else
        {
            for (int r = 0; r < 3; ++r) L[r] = lt_.pos[i][r] - wp[r];
            const float d = Norm3(L);
            const float den = lt_.att[i][0] + lt_.att[i][1] * d + lt_.att[i][2] * d * d;
            att = (den > 0.0f) ? (1.0f / den) : 1.0f;
            if (att > 1.0f) att = 1.0f;
            if (lt_.type[i] == 2)
            {
                float D[3] = { lt_.dir[i][0], lt_.dir[i][1], lt_.dir[i][2] };
                Norm3(D);
                const float cs = -(L[0] * D[0] + L[1] * D[1] + L[2] * D[2]);
                att *= (cs > lt_.spot_cut[i]) ? std::pow(cs, lt_.spot_exp[i]) : 0.0f;
            }
        }
        float la[4], ld[4], ls[4];
        UnpackColor(lt_.col[i][0], la);
        UnpackColor(lt_.col[i][1], ld);
        UnpackColor(lt_.col[i][2], ls);
        float ndl = wn[0] * L[0] + wn[1] * L[1] + wn[2] * L[2];
        if (ndl < 0.0f) ndl = 0.0f;
        if (lt_.comp[i] == 2) ndl = std::pow(ndl, lt_.spow);
        float spec = 0.0f;
        if (lt_.comp[i] == 1 && ndl > 0.0f)
        {
            float H[3] = { L[0], L[1], L[2] + 1.0f };
            Norm3(H);
            const float ndh = wn[0] * H[0] + wn[1] * H[1] + wn[2] * H[2];
            if (ndh > 0.0f) spec = std::pow(ndh, lt_.spow);
        }
        for (int k = 0; k < 3; ++k)
            out[k] += att * (la[k] * ma[k] + ld[k] * md[k] * ndl + ls[k] * ms[k] * spec) / 255.0f;
    }
    uint32_t c = static_cast<uint32_t>(ma[3]) << 24;
    for (int k = 0; k < 3; ++k)
    {
        const int v = (out[k] > 255.0f) ? 255 : static_cast<int>(out[k]);
        c |= static_cast<uint32_t>(v) << (8 * k);
    }
    return c;
}

bool GeInterpreter::ReadVertex(uint32_t addr, uint32_t vtype, int tex_off, int col_off, int nrm_off, int pos_off,
                               Vertex* out)
{
    float pos[3] = {0.0f, 0.0f, 0.0f};
    const int through = static_cast<int>(VtThrough(vtype));
    out->rgba = material_;
    if (col_off >= 0)
    {
        if (VtColor(vtype) == 7)
            out->rgba = Read32(addr + static_cast<uint32_t>(col_off));
        else
            out->rgba = ColorExpand(Read16(addr + static_cast<uint32_t>(col_off)), static_cast<int>(VtColor(vtype)) - 4);
    }

    out->u = out->v = 0.0f;
    if (tex_off >= 0)
    {
        const uint32_t t = addr + static_cast<uint32_t>(tex_off);
        switch (VtTex(vtype))
        {
        case 1:
            out->u = Read8(t);
            out->v = Read8(t + 1);
            if (!through) { out->u /= 128.0f; out->v /= 128.0f; }
            break;
        case 2:
            out->u = Read16(t);
            out->v = Read16(t + 2);
            if (!through) { out->u /= 32768.0f; out->v /= 32768.0f; }
            break;
        default:
            out->u = ReadF32(t);
            out->v = ReadF32(t + 4);
            break;
        }
        if (!through)
        {
            out->u = (out->u * xf_.uscale + xf_.uoff) * static_cast<float>(ds_.tex_w);
            out->v = (out->v * xf_.vscale + xf_.voff) * static_cast<float>(ds_.tex_h);
        }
    }

    if (!VtPos(vtype)) return false;
    Read3(addr + static_cast<uint32_t>(pos_off), static_cast<int>(VtPos(vtype)), through, pos);
    out->behind = 0;
    if (through)
    {
        out->x = static_cast<int>(pos[0]);
        out->y = static_cast<int>(pos[1]);
        out->z = (VtPos(vtype) == 2) ? static_cast<float>(Read16(addr + static_cast<uint32_t>(pos_off) + 4u)) : pos[2];
        return true;
    }
    if (lt_.enable)
    {
        float n[3] = { 0.0f, 0.0f, 1.0f };
        if (nrm_off >= 0) Read3(addr + static_cast<uint32_t>(nrm_off), static_cast<int>(VtNormal(vtype)), 0, n);
        out->rgba = LightVertex(pos, n, out->rgba);
    }
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    if (!TransformPoint(pos, &sx, &sy, &sz))
    {
        out->behind = 1;
        ++stats_.behind_vertices;
        return true;
    }
    out->x = static_cast<int>(std::floor(sx));
    out->y = static_cast<int>(std::floor(sy));
    out->z = sz;
    return true;
}

void GeInterpreter::Reset()
{
    for (auto& q : queues_) q = QueueSlot{};
    for (auto& idx : order_) idx = 0;
    order_count_ = 0;
    next_id_ = kListIdBase;
    stats_ = GeStats{};
    interrupt_cb_ = nullptr;
    list_done_cb_ = nullptr;

    ds_ = DrawState{};
    std::memset(clut_, 0, sizeof(clut_));
    tbp_ = tbw_ = cbp_ = zbp_ = zbw_ = vaddr_ = iaddr_ = 0;
    material_ = 0xFFFFFFFFu;
    ds_.clut = clut_;
    lt_ = LightingState{};
    xf_ = TransformState{};
    xf_.world[0] = xf_.world[4] = xf_.world[8] = 1.0f;
    xf_.view[0] = xf_.view[4] = xf_.view[8] = 1.0f;
    xf_.proj[0] = xf_.proj[5] = xf_.proj[10] = xf_.proj[15] = 1.0f;
    xf_.uscale = xf_.vscale = 1.0f;
    ResetRenderedPixelCount();
}

void GeInterpreter::SetInterruptCallback(GeInterruptCallback cb)
{
    interrupt_cb_ = std::move(cb);
}

void GeInterpreter::SetListDoneCallback(GeListDoneCallback cb)
{
    list_done_cb_ = std::move(cb);
}

bool GeInterpreter::HandleLightCmd(uint32_t cmd, uint32_t arg)
{
    if (cmd >= 0x18 && cmd <= 0x1B) { lt_.on[cmd - 0x18] = arg & 1; return true; }
    if (cmd >= 0x5F && cmd <= 0x62)
    {
        lt_.comp[cmd - 0x5F] = static_cast<int>(arg & 3u);
        lt_.type[cmd - 0x5F] = static_cast<int>((arg >> 8) & 3u);
        return true;
    }
    if (cmd >= 0x63 && cmd <= 0x6E) { lt_.pos[(cmd - 0x63) / 3][(cmd - 0x63) % 3] = F24(arg); return true; }
    if (cmd >= 0x6F && cmd <= 0x7A) { lt_.dir[(cmd - 0x6F) / 3][(cmd - 0x6F) % 3] = F24(arg); return true; }
    if (cmd >= 0x7B && cmd <= 0x86) { lt_.att[(cmd - 0x7B) / 3][(cmd - 0x7B) % 3] = F24(arg); return true; }
    if (cmd >= 0x87 && cmd <= 0x8A) { lt_.spot_exp[cmd - 0x87] = F24(arg); return true; }
    if (cmd >= 0x8B && cmd <= 0x8E) { lt_.spot_cut[cmd - 0x8B] = F24(arg); return true; }
    if (cmd >= 0x8F && cmd <= 0x9A) { lt_.col[(cmd - 0x8F) / 3][(cmd - 0x8F) % 3] = arg; return true; }
    switch (cmd)
    {
    case kGeLte:   lt_.enable = arg & 1; return true;
    case kGeRnorm: lt_.rnorm = arg & 1; return true;
    case kGeCmat:  lt_.cmat = static_cast<int>(arg & 7u); return true;
    case kGeEmc:   lt_.emissive = arg; return true;
    case kGeDmc:   lt_.diffuse = arg; return true;
    case kGeSmc:   lt_.specular = arg; return true;
    case kGeSpow:  lt_.spow = F24(arg); return true;
    case kGeAlc:   lt_.ambient = (lt_.ambient & 0xFF000000u) | arg; return true;
    case kGeAla:   lt_.ambient = (lt_.ambient & 0x00FFFFFFu) | ((arg & 0xFFu) << 24); return true;
    default:       return false;
    }
}

void GeInterpreter::DrawPrim(uint32_t type, uint32_t count)
{
    if (type < kPrimTriangles || type > kPrimSprites)
    {
        ++stats_.unsupported_prims[type & 7u];
        return;
    }
    if (!vaddr_)
    {
        stats_.unsupported_vertices += count;
        return;
    }
    int tex_off = -1, col_off = -1, nrm_off = -1, pos_off = 0;
    const int stride = VertexLayout(stats_.vtype, &tex_off, &col_off, &nrm_off, &pos_off);
    if (!stride)
    {
        stats_.unsupported_vertices += count;
        return;
    }
    static constexpr int kIsz[4] = { 0, 1, 2, 4 };
    const int isz = kIsz[VtIndex(stats_.vtype)];
    if (isz && !iaddr_)
    {
        stats_.unsupported_vertices += count;
        return;
    }

    static Vertex v[65536];
    uint32_t n = 0;
    for (; n < count && n < 65536u; ++n)
    {
        uint32_t idx = n;
        if (isz == 1) idx = Read8(iaddr_ + n);
        else if (isz == 2) idx = Read16(iaddr_ + n * 2u);
        else if (isz == 4) idx = Read32(iaddr_ + n * 4u);
        if (!ReadVertex(vaddr_ + idx * static_cast<uint32_t>(stride), stats_.vtype,
                        tex_off, col_off, nrm_off, pos_off, &v[n]))
        {
            break;
        }
    }
    if (isz) iaddr_ += count * static_cast<uint32_t>(isz);
    else vaddr_ += count * static_cast<uint32_t>(stride);

    if (n > 0)
    {
        GetRenderBackend()->draw(static_cast<int>(type), v, static_cast<int>(n), &ds_);
    }
}

void GeInterpreter::FinishSlot(QueueSlot* q)
{
    q->expect_signal_end = false;
    q->done = true;
    q->used = false;
    if (GetRenderBackend()->finish) GetRenderBackend()->finish();
    if (list_done_cb_) list_done_cb_(q->id);
}

void GeInterpreter::RunList(QueueSlot* q)
{
    uint64_t budget = 1u << 22;

    if (!q->started)
    {
        q->started = true;
        ++stats_.lists;
    }
    q->stalled = false;
    q->paused = false;

    while (budget--)
    {
        if (q->stall && q->list == q->stall)
        {
            q->stalled = true;
            return;
        }

        const uint32_t word = Read32(q->list);
        const uint32_t cmd = word >> 24;
        const uint32_t arg = word & 0x00FFFFFFu;
        q->list += 4u;
        ++stats_.commands;

        switch (cmd)
        {
        case kGeNop:
            break;
        case kGePrim:
        {
            const uint32_t type = (arg >> 16) & 7u;
            const uint32_t count = arg & 0xFFFFu;
            ++stats_.prims[type];
            stats_.vertices += count;
            DrawPrim(type, count);
            break;
        }
        case kGeBezier:
            ++stats_.unsupported_bezier;
            break;
        case kGeSpline:
            ++stats_.unsupported_spline;
            break;
        case kGeJump:
            q->list = ResolveGeAddr(q->base, q->offset, arg) & ~3u;
            break;
        case kGeCall:
            if (q->call_sp >= kGeCallStackSize)
            {
                platform::RaiseStop("GE CALL stack overflow");
                return;
            }
            q->call_stack[q->call_sp++] = q->list;
            q->list = ResolveGeAddr(q->base, q->offset, arg) & ~3u;
            break;
        case kGeRet:
            if (q->call_sp <= 0)
            {
                platform::RaiseStop("GE RET on empty call stack");
                return;
            }
            q->list = q->call_stack[--q->call_sp];
            break;
        case kGeBjump:
            break;
        case kGeEnd:
            if (q->expect_signal_end)
            {
                q->expect_signal_end = false;
                break;
            }
            FinishSlot(q);
            return;
        case kGeFinish:
            ++stats_.finishes;
            if (interrupt_cb_ && q->cb_id >= 0)
            {
                interrupt_cb_(GeInterruptEvent::kFinish, q->cb_id, arg & 0xFFFFu);
            }
            FinishSlot(q);
            return;
        case kGeSignal:
        {
            const uint32_t behavior = (arg >> 16) & 0xFFu;
            const uint32_t sig_arg = arg & 0xFFFFu;
            ++stats_.signals;
            if (behavior == 0x00u || behavior == 0x01u || behavior == 0x02u)
            {
                q->expect_signal_end = true;
                if (interrupt_cb_ && q->cb_id >= 0)
                {
                    interrupt_cb_(GeInterruptEvent::kSignal, q->cb_id, sig_arg);
                }
            }
            else if (behavior == 0x03u)
            {
                q->expect_signal_end = true;
                if (interrupt_cb_ && q->cb_id >= 0)
                {
                    interrupt_cb_(GeInterruptEvent::kSignal, q->cb_id, sig_arg);
                }
                q->paused = true;
                return;
            }
            else
            {
                char msg[96];
                std::snprintf(msg, sizeof(msg), "Unsupported GE SIGNAL behavior 0x%02X (arg=0x%04X)", behavior, sig_arg);
                platform::RaiseStop(msg);
                return;
            }
            break;
        }
        case kGeBase:       q->base = (arg & 0xFF0000u) << 8; break;
        case kGeOriginAddr: q->offset = q->list - 4u; break;
        case kGeOffsetAddr: q->offset = arg << 8; break;
        case kGeVtype:      stats_.vtype = arg; break;
        case kGeFbp:
            stats_.fbp = (stats_.fbp & 0xFF000000u) | arg;
            GetRenderBackend()->set_target(FbVramAddr(stats_.fbp), stats_.fbw, static_cast<int>(stats_.psm));
            break;
        case kGeFbw:
            stats_.fbw = arg & 0xFFFFu;
            stats_.fbp = (stats_.fbp & 0x00FFFFFFu) | ((arg & 0xFF0000u) << 8);
            GetRenderBackend()->set_target(FbVramAddr(stats_.fbp), stats_.fbw, static_cast<int>(stats_.psm));
            break;
        case kGePsm:
            stats_.psm = arg & 3u;
            GetRenderBackend()->set_target(FbVramAddr(stats_.fbp), stats_.fbw, static_cast<int>(stats_.psm));
            break;
        case kGeWms:   xf_.wi = static_cast<int>(arg & 0xFu); break;
        case kGeWorld: if (xf_.wi < 12) xf_.world[xf_.wi++] = F24(arg); break;
        case kGeVms:   xf_.vi = static_cast<int>(arg & 0xFu); break;
        case kGeView:  if (xf_.vi < 12) xf_.view[xf_.vi++] = F24(arg); break;
        case kGePms:   xf_.pi = static_cast<int>(arg & 0xFu); break;
        case kGeProj:  if (xf_.pi < 16) xf_.proj[xf_.pi++] = F24(arg); break;
        case kGeXscale:  xf_.xscale = F24(arg); break;
        case kGeYscale:  xf_.yscale = F24(arg); break;
        case kGeXpos:    xf_.xpos = F24(arg); break;
        case kGeYpos:    xf_.ypos = F24(arg); break;
        case kGeUscale:  xf_.uscale = F24(arg); break;
        case kGeVscale:  xf_.vscale = F24(arg); break;
        case kGeUoffset: xf_.uoff = F24(arg); break;
        case kGeVoffset: xf_.voff = F24(arg); break;
        case kGeOffsetX: xf_.offx = static_cast<float>(arg & 0xFFFFu) / 16.0f; break;
        case kGeOffsetY: xf_.offy = static_cast<float>(arg & 0xFFFFu) / 16.0f; break;
        case kGeTme:   ds_.tex_enable = arg & 1; break;
        case kGeAbe:   ds_.blend = arg & 1; break;
        case kGeAte:   ds_.atest = arg & 1; break;
        case kGeCte:   ds_.ctest = arg & 1; break;
        case kGeShade: ds_.gouraud = arg & 1; break;
        case kGeAmc:   material_ = (material_ & 0xFF000000u) | arg; break;
        case kGeAma:   material_ = (material_ & 0x00FFFFFFu) | ((arg & 0xFFu) << 24); break;
        case kGeClear:
            ds_.clear = arg & 1;
            ds_.clear_color = (arg >> 8) & 1;
            ds_.clear_alpha = (arg >> 9) & 1;
            ds_.clear_depth = (arg >> 10) & 1;
            break;
        case kGeZte:    ds_.ztest = arg & 1; break;
        case kGeZtst:   ds_.zfunc = static_cast<int>(arg & 7u); break;
        case kGeZmsk:   ds_.zwrite = !(arg & 1); break;
        case kGeZscale: xf_.zscale = F24(arg); break;
        case kGeZpos:   xf_.zpos = F24(arg); break;
        case kGeZbp:
        case kGeZbw:
            if (cmd == kGeZbp) zbp_ = arg; else zbw_ = arg;
            ds_.zaddr = runtime::GuestMemory::kVramAddress |
                         ((((zbw_ & 0xFF0000u) << 8) | zbp_) & (runtime::GuestMemory::kVramSize - 1u));
            ds_.zbw = static_cast<int>(zbw_ & 0xFFFFu);
            break;
        case kGeTbp0:
            tbp_ = arg;
            ds_.tex_addr = ((tbw_ & 0xFF0000u) << 8) | tbp_;
            break;
        case kGeTbw0:
            tbw_ = arg;
            ds_.tex_bw = static_cast<int>(arg & 0xFFFFu);
            ds_.tex_addr = ((tbw_ & 0xFF0000u) << 8) | tbp_;
            break;
        case kGeTsize0:
            ds_.tex_w = 1 << (arg & 0xFu);
            ds_.tex_h = 1 << ((arg >> 8) & 0xFu);
            break;
        case kGeTmode: ds_.tex_swizzle = arg & 1; break;
        case kGeTpsm:  ds_.tex_fmt = static_cast<int>(arg & 0xFu); break;
        case kGeTwrap:
            ds_.tex_clamp_u = arg & 1;
            ds_.tex_clamp_v = (arg >> 8) & 1;
            break;
        case kGeTfunc:
            ds_.tex_func = static_cast<int>(arg & 7u);
            ds_.tex_alpha = (arg >> 8) & 1;
            ds_.tex_double = (arg >> 16) & 1;
            break;
        case kGeTec:  ds_.tex_env = arg; break;
        case kGeCbp:  cbp_ = (cbp_ & 0xFF000000u) | arg; break;
        case kGeCbph: cbp_ = (cbp_ & 0x00FFFFFFu) | ((arg & 0x0F0000u) << 8); break;
        case kGeCload:
        {
            uint32_t n = (arg & 0x3Fu) * 32u;
            if (n > sizeof(clut_)) n = sizeof(clut_);
            for (uint32_t i = 0; i < n; ++i) clut_[i] = Read8(cbp_ + i);
            break;
        }
        case kGeCmode:
            ds_.clut_fmt = static_cast<int>(arg & 3u);
            ds_.clut_shift = static_cast<int>((arg >> 2) & 0x1Fu);
            ds_.clut_mask = static_cast<int>((arg >> 8) & 0xFFu);
            ds_.clut_start = static_cast<int>((arg >> 16) & 0x1Fu);
            break;
        case kGeScissor1:
            ds_.sc_x0 = static_cast<int>(arg & 0x3FFu);
            ds_.sc_y0 = static_cast<int>((arg >> 10) & 0x3FFu);
            break;
        case kGeScissor2:
            ds_.sc_x1 = static_cast<int>(arg & 0x3FFu);
            ds_.sc_y1 = static_cast<int>((arg >> 10) & 0x3FFu);
            break;
        case kGeCtest: ds_.ctest_func = static_cast<int>(arg & 3u); break;
        case kGeCref:  ds_.ctest_ref = arg & 0x00FFFFFFu; break;
        case kGeCmsk:  ds_.ctest_mask = arg & 0x00FFFFFFu; break;
        case kGeAtst:
            ds_.atest_func = static_cast<int>(arg & 7u);
            ds_.atest_ref = static_cast<int>((arg >> 8) & 0xFFu);
            ds_.atest_mask = static_cast<int>((arg >> 16) & 0xFFu);
            break;
        case kGeAlpha:
            ds_.blend_src = static_cast<int>(arg & 0xFu);
            ds_.blend_dst = static_cast<int>((arg >> 4) & 0xFu);
            ds_.blend_op = static_cast<int>((arg >> 8) & 0xFu);
            break;
        case kGeSfix:  ds_.blend_sfix = arg; break;
        case kGeDfix:  ds_.blend_dfix = arg; break;
        case kGeVaddr: vaddr_ = ResolveGeAddr(q->base, q->offset, arg); break;
        case kGeIaddr: iaddr_ = ResolveGeAddr(q->base, q->offset, arg); break;
        default:
            if (HandleLightCmd(cmd, arg)) break;
            ++stats_.unknown_commands;
            ++stats_.op_count[cmd];
            stats_.op_last[cmd] = arg;
            break;
        }
    }

    platform::RaiseStop("GE command budget exhausted");
}

GeInterpreter::QueueSlot* GeInterpreter::FindQueue(int32_t list_id)
{
    const uint32_t uid = static_cast<uint32_t>(list_id);
    for (auto& q : queues_)
    {
        if (q.used && q.id == uid) return &q;
    }
    return nullptr;
}

const GeInterpreter::QueueSlot* GeInterpreter::FindQueue(int32_t list_id) const
{
    const uint32_t uid = static_cast<uint32_t>(list_id);
    for (const auto& q : queues_)
    {
        if (q.used && q.id == uid) return &q;
    }
    return nullptr;
}

void GeInterpreter::DrainQueue()
{
    while (order_count_ > 0)
    {
        QueueSlot* q = &queues_[order_[0]];
        if (!q->used || q->done)
        {
            for (int i = 1; i < order_count_; ++i)
            {
                order_[i - 1] = order_[i];
            }
            --order_count_;
            continue;
        }
        if (q->stalled || q->paused)
        {
            return;
        }
        RunList(q);
        if (!q->used || q->done)
        {
            for (int i = 1; i < order_count_; ++i)
            {
                order_[i - 1] = order_[i];
            }
            --order_count_;
            continue;
        }
        return;
    }
}

int32_t GeInterpreter::Enqueue(uint32_t list_addr, uint32_t stall_addr, int32_t cb_id, uint32_t arg, bool head)
{
    int slot_idx = -1;
    for (int i = 0; i < kMaxQueues; ++i)
    {
        if (!queues_[i].used)
        {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx < 0 || order_count_ >= kMaxQueues) return kErrNoMemory;

    QueueSlot* q = &queues_[slot_idx];
    *q = QueueSlot{};
    q->id = next_id_++;
    q->list = list_addr & ~3u;
    q->stall = stall_addr & ~3u;
    q->cb_id = cb_id;
    q->arg = arg;
    q->used = true;

    if (head && order_count_ > 0)
    {
        const int insert_pos = (queues_[order_[0]].started) ? 1 : 0;
        for (int i = order_count_; i > insert_pos; --i)
        {
            order_[i] = order_[i - 1];
        }
        order_[insert_pos] = slot_idx;
    }
    else
    {
        order_[order_count_] = slot_idx;
    }
    ++order_count_;

    DrainQueue();
    return static_cast<int32_t>(q->id);
}

int32_t GeInterpreter::UpdateStallAddr(int32_t list_id, uint32_t stall_addr)
{
    QueueSlot* q = FindQueue(list_id);
    if (!q)
    {
        const uint32_t uid = static_cast<uint32_t>(list_id);
        return (uid >= kListIdBase && uid < next_id_) ? 0 : kErrUnknownUid;
    }
    q->stall = stall_addr & ~3u;
    if (order_count_ > 0 && &queues_[order_[0]] == q && !q->done && !q->paused)
    {
        q->stalled = false;
        DrainQueue();
    }
    return 0;
}

int32_t GeInterpreter::GetListState(int32_t list_id) const
{
    const QueueSlot* q = FindQueue(list_id);
    if (!q)
    {
        const uint32_t uid = static_cast<uint32_t>(list_id);
        return (uid >= kListIdBase && uid < next_id_) ? kGeListDone : kErrUnknownUid;
    }
    if (q->done) return kGeListDone;
    if (q->paused) return kGeListPaused;
    if (q->stalled) return kGeListStalled;
    return kGeListQueued;
}

bool GeInterpreter::HasPendingLists() const
{
    for (const auto& q : queues_)
    {
        if (q.used && !q.done) return true;
    }
    return false;
}

int32_t GeInterpreter::Break(int32_t mode)
{
    (void)mode;
    return 0;
}

int32_t GeInterpreter::Continue()
{
    if (order_count_ > 0)
    {
        QueueSlot* q = &queues_[order_[0]];
        if (q->used && q->paused && !q->done)
        {
            q->paused = false;
            DrainQueue();
        }
    }
    return 0;
}

std::string GeInterpreter::FormatStats() const
{
    std::ostringstream oss;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "GE: %llu lists, %llu commands, %llu signals, %llu finishes",
                  static_cast<unsigned long long>(stats_.lists),
                  static_cast<unsigned long long>(stats_.commands),
                  static_cast<unsigned long long>(stats_.signals),
                  static_cast<unsigned long long>(stats_.finishes));
    oss << buf << "\n";
    std::snprintf(buf, sizeof(buf),
                  "    framebuffer 0x%08X stride %u psm %u, vertex type 0x%06X",
                  stats_.fbp, stats_.fbw, stats_.psm, stats_.vtype);
    oss << buf << "\n";
    oss << "    vertices submitted: " << stats_.vertices << "\n";
    for (int i = 0; i < 8; ++i)
    {
        if (stats_.prims[i] > 0)
        {
            std::snprintf(buf, sizeof(buf), "    %-15s %llu", kPrimNames[i],
                          static_cast<unsigned long long>(stats_.prims[i]));
            oss << buf << "\n";
        }
    }
    oss << "    pixels written: " << GetRenderedPixelCount() << "\n";
    if (stats_.unknown_commands > 0)
    {
        oss << "    " << stats_.unknown_commands << " commands not individually decoded:\n";
        for (int i = 0; i < 256; ++i)
        {
            if (stats_.op_count[i] > 0)
            {
                std::snprintf(buf, sizeof(buf), "      0x%02X x%-8llu last arg 0x%06X",
                              i, static_cast<unsigned long long>(stats_.op_count[i]), stats_.op_last[i]);
                oss << buf << "\n";
            }
        }
    }
    if (stats_.behind_vertices > 0)
    {
        oss << "    " << stats_.behind_vertices << " vertices behind the eye; triangles dropped\n";
    }
    if (GetUnsupportedTexelCount() > 0)
    {
        oss << "    " << GetUnsupportedTexelCount() << " unsupported texels drawn magenta\n";
    }
    if (stats_.unsupported_vertices > 0)
    {
        oss << "    " << stats_.unsupported_vertices << " vertices in unsupported format\n";
    }
    for (int i = 0; i < 8; ++i)
    {
        if (stats_.unsupported_prims[i] > 0)
        {
            oss << "    unsupported " << kPrimNames[i] << ": " << stats_.unsupported_prims[i] << "\n";
        }
    }
    if (stats_.unsupported_bezier > 0)
    {
        oss << "    unsupported bezier: " << stats_.unsupported_bezier << "\n";
    }
    if (stats_.unsupported_spline > 0)
    {
        oss << "    unsupported spline: " << stats_.unsupported_spline << "\n";
    }
    if (GetOutOfBoundsVramWriteCount() > 0)
    {
        oss << "    " << GetOutOfBoundsVramWriteCount() << " out-of-bounds VRAM writes dropped\n";
    }
    return oss.str();
}

void GeInterpreter::DumpStats(FILE* out) const
{
    if (!out) return;
    const std::string s = FormatStats();
    std::fwrite(s.data(), 1, s.size(), out);
}

GeInterpreter& GetGeInterpreter()
{
    static GeInterpreter instance;
    return instance;
}

} // namespace repsp::gpu
