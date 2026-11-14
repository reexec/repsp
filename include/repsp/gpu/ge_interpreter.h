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

struct GeStats
{
    uint64_t lists = 0;
    uint64_t commands = 0;
    uint64_t prims[8] = {0};
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

class GeInterpreter
{
public:
    static constexpr uint32_t kListIdBase = 0x00080000u;

    void Reset();
    void SetInterruptCallback(GeInterruptCallback cb);

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
        uint32_t origin = 0;
        int32_t cb_id = -1;
        uint32_t arg = 0;
        bool used = false;
        bool done = false;
        bool stalled = false;
        bool paused = false;
    };

    QueueSlot* FindQueue(int32_t list_id);
    const QueueSlot* FindQueue(int32_t list_id) const;
    void RunList(QueueSlot* q);
    void DrawPrim(uint32_t type, uint32_t count);
    bool HandleLightCmd(uint32_t cmd, uint32_t arg);

    QueueSlot queues_[kMaxQueues];
    uint32_t next_id_ = kListIdBase;
    GeStats stats_;
    GeInterruptCallback interrupt_cb_;
};

GeInterpreter& GetGeInterpreter();

} // namespace repsp::gpu

#endif // REPSP_GPU_GE_INTERPRETER_H
