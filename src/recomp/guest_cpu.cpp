#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"

namespace
{

const repsp_guest_cpu_ops* g_cpu_ops = nullptr;

} // namespace

void repsp_recomp_register_cpu(const repsp_guest_cpu_ops* ops)
{
    g_cpu_ops = ops;
    repsp::runtime::GetScheduler().SetCpuOps(ops);
}

const repsp_guest_cpu_ops* repsp_recomp_get_cpu_ops(void)
{
    return g_cpu_ops;
}

void repsp_recomp_hle_call(uint32_t nid)
{
    if (!repsp::runtime::GetNidRegistry().Invoke(nid))
    {
        if (g_cpu_ops != nullptr && g_cpu_ops->set_gpr != nullptr)
        {
            g_cpu_ops->set_gpr(2, repsp::runtime::kSceKernelErrorNotImplemented);
        }
    }
    repsp::runtime::GetScheduler().DeliverPendingInterrupts();
}

void repsp_recomp_syscall(uint32_t code)
{
    repsp::runtime::Log(repsp::runtime::LogLevel::kDebug, "[RECOMP] Syscall instruction: 0x%08X", code);
}

void repsp_recomp_poll(void)
{
    // Safe point for scheduler baton pass or interrupt delivery
    repsp::runtime::GetScheduler().Poll();
}

uint8_t* repsp_guest_base(void)
{
    return repsp::runtime::GetGuestMemory().Base();
}

uint32_t repsp_guest_mask(void)
{
    return repsp::runtime::GuestMemory::kFoldMask;
}

uint8_t* repsp_guest_translate(uint32_t addr)
{
    return repsp::runtime::GetGuestMemory().Translate(addr);
}

uint8_t repsp_guest_read8(uint32_t addr)
{
    return repsp::runtime::GetGuestMemory().ReadU8(addr);
}

uint16_t repsp_guest_read16(uint32_t addr)
{
    return repsp::runtime::GetGuestMemory().ReadU16(addr);
}

uint32_t repsp_guest_read32(uint32_t addr)
{
    return repsp::runtime::GetGuestMemory().ReadU32(addr);
}

uint64_t repsp_guest_read64(uint32_t addr)
{
    return repsp::runtime::GetGuestMemory().ReadU64(addr);
}

void repsp_guest_write8(uint32_t addr, uint8_t val)
{
    repsp::runtime::GetGuestMemory().WriteU8(addr, val);
}

void repsp_guest_write16(uint32_t addr, uint16_t val)
{
    repsp::runtime::GetGuestMemory().WriteU16(addr, val);
}

void repsp_guest_write32(uint32_t addr, uint32_t val)
{
    repsp::runtime::GetGuestMemory().WriteU32(addr, val);
}

void repsp_guest_write64(uint32_t addr, uint64_t val)
{
    repsp::runtime::GetGuestMemory().WriteU64(addr, val);
}
