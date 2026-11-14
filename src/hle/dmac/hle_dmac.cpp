#include "repsp/hle/dmac.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"

#include <cstdint>
#include <cstring>

namespace repsp::hle::dmac
{

namespace
{

void HleDmacMemcpy()
{
    const uint32_t dst_addr = recomp::Arg<uint32_t>(0);
    const uint32_t src_addr = recomp::Arg<uint32_t>(1);
    const uint32_t size = recomp::Arg<uint32_t>(2);

    if (size == 0)
    {
        recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorInvalidSize));
        return;
    }

    auto& mem = runtime::GetGuestMemory();
    if (dst_addr == 0 || src_addr == 0 ||
        size > runtime::GuestMemory::kUserRamSize ||
        dst_addr > UINT32_MAX - (size - 1) ||
        src_addr > UINT32_MAX - (size - 1) ||
        !mem.IsValidAddress(dst_addr, size) ||
        !mem.IsValidAddress(src_addr, size))
    {
        recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorInvalidPointer));
        return;
    }

    uint8_t* dst = mem.Translate(dst_addr);
    const uint8_t* src = mem.Translate(src_addr);
    std::memmove(dst, src, size);
    recomp::SetGpr(2, static_cast<uint32_t>(kSceKernelErrorOk));
}

} // namespace

void Register()
{
    auto& reg = runtime::GetNidRegistry();
    reg.Register("sceDmac", 0x617F3FE6u, "sceDmacMemcpy", HleDmacMemcpy);
}

void Shutdown()
{
}

} // namespace repsp::hle::dmac
