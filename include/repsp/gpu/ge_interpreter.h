// Adapted from sp00nznet/psprecomp (MIT License, commit 5ca18fa, src/hle/ge.c).
// Reshaped to repsp types and namespace with no dependency on psprecomp headers.

#ifndef REPSP_GPU_GE_INTERPRETER_H
#define REPSP_GPU_GE_INTERPRETER_H

#include "repsp/gpu/render_backend.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace repsp::gpu
{

enum GeListState : int32_t
{
    kGeListDone = 0,
    kGeListQueued = 1,
    kGeListRunning = 2,
    kGeListStalled = 3,
    kGeListPaused = 4
};

enum class GeInterruptEvent
{
    kSignal = 0,
    kFinish = 1
};

using GeInterruptCallback = std::function<void(GeInterruptEvent event, int32_t cb_id, uint32_t arg)>;
using GeListDoneCallback = std::function<void(uint32_t list_id)>;

struct GeStats
{
    uint64_t lists = 0;
    uint64_t commands = 0;
    uint64_t prims[8] = {0};
    uint64_t unsupported_prims[8] = {0};
    uint64_t unsupported_bezier = 0;
    uint64_t unsupported_spline = 0;
    uint64_t vertices = 0;
    uint64_t signals = 0;
    uint64_t finishes = 0;
    uint64_t unknown_commands = 0;
    uint64_t op_count[256] = {0};
    uint32_t op_last[256] = {0};
    uint64_t behind_vertices = 0;
    uint64_t unsupported_vertices = 0;
    uint32_t fbp = 0;
    uint32_t fbw = 0;
    uint32_t psm = 3;
    uint32_t vtype = 0;
};

struct LightingState
{
    int enable = 0;
    int on[4] = {0};
    int type[4] = {0};
    int comp[4] = {0};
    int cmat = 0;
    int rnorm = 0;
    uint32_t emissive = 0;
    uint32_t diffuse = 0;
    uint32_t specular = 0;
    uint32_t ambient = 0;
    float spow = 0.0f;
    float pos[4][3] = {{0}};
    float dir[4][3] = {{0}};
    float att[4][3] = {{0}};
    float spot_exp[4] = {0};
    float spot_cut[4] = {0};
    uint32_t col[4][3] = {{0}};
};

struct TransformState
{
    float world[12] = {0};
    float view[12] = {0};
    float proj[16] = {0};
    int wi = 0;
    int vi = 0;
    int pi = 0;
    float xscale = 0.0f;
    float yscale = 0.0f;
    float xpos = 0.0f;
    float ypos = 0.0f;
    float offx = 0.0f;
    float offy = 0.0f;
    float uscale = 1.0f;
    float vscale = 1.0f;
    float uoff = 0.0f;
    float voff = 0.0f;
    float zscale = 0.0f;
    float zpos = 0.0f;
};

class GeInterpreter
{
public:
    static constexpr uint32_t kListIdBase = 0x00080000u;

    void Reset();
    void SetInterruptCallback(GeInterruptCallback cb);
    void SetListDoneCallback(GeListDoneCallback cb);

    // Enqueue a display list and run synchronously until END/FINISH or stall.
    // Returns positive list UID on success, or negative kernel error code.
    int32_t Enqueue(uint32_t list_addr, uint32_t stall_addr, int32_t cb_id, uint32_t arg, bool head = false);

    // Update stall address for a queued/stalled list and resume execution if needed.
    int32_t UpdateStallAddr(int32_t list_id, uint32_t stall_addr);

    // Query list state (kGeListDone .. kGeListPaused).
    int32_t GetListState(int32_t list_id) const;

    // True if any active list is currently stalled or paused.
    bool HasPendingLists() const;

    int32_t Break(int32_t mode);
    int32_t Continue();

    const GeStats& GetStats() const { return stats_; }
    void DumpStats(FILE* out) const;
    std::string FormatStats() const;

private:
    static constexpr int kMaxQueues = 8;
    static constexpr int kGeCallStackSize = 8;

    struct QueueSlot
    {
        uint32_t id = 0;
        uint32_t list = 0;
        uint32_t stall = 0;
        uint32_t base = 0;
        uint32_t offset = 0;
        int32_t cb_id = -1;
        uint32_t arg = 0;
        uint32_t call_stack[kGeCallStackSize] = {0};
        int call_sp = 0;
        bool expect_signal_end = false;
        bool used = false;
        bool started = false;
        bool done = false;
        bool stalled = false;
        bool paused = false;
    };

    QueueSlot* FindQueue(int32_t list_id);
    const QueueSlot* FindQueue(int32_t list_id) const;
    void DrainQueue();
    void FinishSlot(QueueSlot* q);
    void RunList(QueueSlot* q);
    void DrawPrim(uint32_t type, uint32_t count);
    bool HandleLightCmd(uint32_t cmd, uint32_t arg);
    bool TransformPoint(const float p[3], float* sx, float* sy, float* sz) const;
    uint32_t LightVertex(const float p[3], const float n[3], uint32_t vcol) const;
    bool ReadVertex(uint32_t addr, uint32_t vtype, int tex_off, int col_off, int nrm_off, int pos_off, Vertex* out);

    QueueSlot queues_[kMaxQueues];
    int order_[kMaxQueues] = {0};
    int order_count_ = 0;
    uint32_t next_id_ = kListIdBase;
    GeStats stats_;
    GeInterruptCallback interrupt_cb_;
    GeListDoneCallback list_done_cb_;

    DrawState ds_;
    LightingState lt_;
    TransformState xf_;
    uint32_t tbp_ = 0;
    uint32_t tbw_ = 0;
    uint32_t cbp_ = 0;
    uint32_t zbp_ = 0;
    uint32_t zbw_ = 0;
    uint32_t vaddr_ = 0;
    uint32_t iaddr_ = 0;
    uint8_t clut_[1024] = {0};
    uint32_t material_ = 0xFFFFFFFFu;
};

GeInterpreter& GetGeInterpreter();

} // namespace repsp::gpu

#endif // REPSP_GPU_GE_INTERPRETER_H
