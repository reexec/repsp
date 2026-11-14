#include "repsp/hle/ge.h"
#include "repsp/gpu/ge_interpreter.h"
#include "repsp/gpu/render_backend.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace repsp::hle::ge
{

namespace
{

constexpr int32_t kErrInvalidId = static_cast<int32_t>(0x800200CBu);
constexpr int32_t kErrNoMemory = static_cast<int32_t>(0x800200D9u);
constexpr int kMaxCallbacks = 16;

struct GeCallbackSlot
{
    bool used = false;
    uint32_t signal_func = 0;
    uint32_t signal_arg = 0;
    uint32_t finish_func = 0;
    uint32_t finish_arg = 0;
    uint32_t gp_value = 0;
};

std::mutex g_ge_mutex;
std::array<GeCallbackSlot, kMaxCallbacks> g_callbacks;

void OnGeInterrupt(gpu::GeInterruptEvent event, int32_t cb_id, uint32_t arg)
{
    uint32_t func = 0;
    uint32_t user_arg = 0;
    uint32_t gp_val = 0;

    {
        std::lock_guard<std::mutex> lock(g_ge_mutex);
        if (cb_id < 0 || cb_id >= kMaxCallbacks || !g_callbacks[cb_id].used)
        {
            return;
        }
        const auto& slot = g_callbacks[cb_id];
        gp_val = slot.gp_value;
        if (event == gpu::GeInterruptEvent::kSignal)
        {
            func = slot.signal_func;
            user_arg = slot.signal_arg;
        }
        else
        {
            func = slot.finish_func;
            user_arg = slot.finish_arg;
        }
    }

    if (func != 0)
    {
        runtime::GetScheduler().QueueInterrupt(func, arg, user_arg, gp_val);
    }
}

void HleEdramGetAddr()
{
    recomp::Return(static_cast<uint32_t>(runtime::GuestMemory::kVramAddress));
}

void HleEdramGetSize()
{
    recomp::Return(static_cast<uint32_t>(runtime::GuestMemory::kVramSize));
}

void HleSetCallback()
{
    const uint32_t cb_addr = recomp::Arg<uint32_t>(0);
    if (cb_addr == 0 || !runtime::GetGuestMemory().IsValidAddress(cb_addr, 16))
    {
        recomp::Return(kErrInvalidId);
        return;
    }

    auto& mem = runtime::GetGuestMemory();
    const uint32_t sig_func = mem.ReadU32(cb_addr + 0);
    const uint32_t sig_arg = mem.ReadU32(cb_addr + 4);
    const uint32_t fin_func = mem.ReadU32(cb_addr + 8);
    const uint32_t fin_arg = mem.ReadU32(cb_addr + 12);
    const uint32_t gp_val = runtime::GetScheduler().GetCurrentThreadGp();

    std::lock_guard<std::mutex> lock(g_ge_mutex);
    for (int i = 0; i < kMaxCallbacks; ++i)
    {
        if (!g_callbacks[i].used)
        {
            g_callbacks[i].used = true;
            g_callbacks[i].signal_func = sig_func;
            g_callbacks[i].signal_arg = sig_arg;
            g_callbacks[i].finish_func = fin_func;
            g_callbacks[i].finish_arg = fin_arg;
            g_callbacks[i].gp_value = gp_val;
            recomp::Return(static_cast<int32_t>(i));
            return;
        }
    }
    recomp::Return(kErrNoMemory);
}

void HleUnsetCallback()
{
    const int32_t cb_id = static_cast<int32_t>(recomp::Arg<uint32_t>(0));
    std::lock_guard<std::mutex> lock(g_ge_mutex);
    if (cb_id < 0 || cb_id >= kMaxCallbacks || !g_callbacks[cb_id].used)
    {
        recomp::Return(kErrInvalidId);
        return;
    }
    g_callbacks[cb_id] = GeCallbackSlot{};
    recomp::Return(0);
}

void HleListEnQueue()
{
    const uint32_t list_addr = recomp::Arg<uint32_t>(0);
    const uint32_t stall_addr = recomp::Arg<uint32_t>(1);
    const int32_t cb_id = static_cast<int32_t>(recomp::Arg<uint32_t>(2));
    const uint32_t arg = recomp::Arg<uint32_t>(3);
    recomp::Return(gpu::GetGeInterpreter().Enqueue(list_addr, stall_addr, cb_id, arg, false));
}

void HleListEnQueueHead()
{
    const uint32_t list_addr = recomp::Arg<uint32_t>(0);
    const uint32_t stall_addr = recomp::Arg<uint32_t>(1);
    const int32_t cb_id = static_cast<int32_t>(recomp::Arg<uint32_t>(2));
    const uint32_t arg = recomp::Arg<uint32_t>(3);
    recomp::Return(gpu::GetGeInterpreter().Enqueue(list_addr, stall_addr, cb_id, arg, true));
}

void HleListUpdateStallAddr()
{
    const int32_t list_id = static_cast<int32_t>(recomp::Arg<uint32_t>(0));
    const uint32_t stall_addr = recomp::Arg<uint32_t>(1);
    recomp::Return(gpu::GetGeInterpreter().UpdateStallAddr(list_id, stall_addr));
}

void HleListSync()
{
    const int32_t list_id = static_cast<int32_t>(recomp::Arg<uint32_t>(0));
    const int32_t mode = static_cast<int32_t>(recomp::Arg<uint32_t>(1));
    if (mode == 1)
    {
        recomp::Return(gpu::GetGeInterpreter().GetListState(list_id));
        return;
    }
    // Immediate execution: list is already complete unless stalled.
    recomp::Return(0);
}

void HleDrawSync()
{
    const int32_t mode = static_cast<int32_t>(recomp::Arg<uint32_t>(0));
    if (mode == 1)
    {
        recomp::Return(gpu::GetGeInterpreter().HasPendingLists()
            ? static_cast<int32_t>(gpu::kGeListStalled)
            : static_cast<int32_t>(gpu::kGeListDone));
        return;
    }
    recomp::Return(0);
}

void HleBreak()
{
    const int32_t mode = static_cast<int32_t>(recomp::Arg<uint32_t>(0));
    recomp::Return(gpu::GetGeInterpreter().Break(mode));
}

void HleContinue()
{
    recomp::Return(gpu::GetGeInterpreter().Continue());
}

} // namespace

void Reset()
{
    {
        std::lock_guard<std::mutex> lock(g_ge_mutex);
        for (auto& cb : g_callbacks)
        {
            cb = GeCallbackSlot{};
        }
    }
    gpu::GetGeInterpreter().Reset();
    gpu::GetGeInterpreter().SetInterruptCallback(OnGeInterrupt);
    if (gpu::GetRenderBackend()->init)
    {
        gpu::GetRenderBackend()->init(480, 272);
    }
}

void Register()
{
    Reset();

    auto& reg = runtime::GetNidRegistry();
    reg.Register("sceGe_user", 0xE47E40E4u, "sceGeEdramGetAddr", HleEdramGetAddr);
    reg.Register("sceGe_user", 0x1F6752ADu, "sceGeEdramGetSize", HleEdramGetSize);
    reg.Register("sceGe_user", 0xA4FC06A4u, "sceGeSetCallback", HleSetCallback);
    reg.Register("sceGe_user", 0x05DB22CEu, "sceGeUnsetCallback", HleUnsetCallback);
    reg.Register("sceGe_user", 0xAB49E76Au, "sceGeListEnQueue", HleListEnQueue);
    reg.Register("sceGe_user", 0x1C0D95A6u, "sceGeListEnQueueHead", HleListEnQueueHead);
    reg.Register("sceGe_user", 0xE0D68148u, "sceGeListUpdateStallAddr", HleListUpdateStallAddr);
    reg.Register("sceGe_user", 0x03444EB4u, "sceGeListSync", HleListSync);
    reg.Register("sceGe_user", 0xB287BD61u, "sceGeDrawSync", HleDrawSync);
    reg.Register("sceGe_user", 0xB448EC0Du, "sceGeBreak", HleBreak);
    reg.Register("sceGe_user", 0x4C06E472u, "sceGeContinue", HleContinue);
}

const gpu::GeStats& GetStats()
{
    return gpu::GetGeInterpreter().GetStats();
}

std::string FormatStats()
{
    return gpu::GetGeInterpreter().FormatStats();
}

} // namespace repsp::hle::ge
