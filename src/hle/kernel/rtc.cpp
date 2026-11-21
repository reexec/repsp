#include "kernel_internal.h"
#include "repsp/hle/kernel_errors.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/nid_registry.h"

#include <chrono>
#include <cstdint>
#include <ctime>

namespace repsp::hle::kernel::detail
{

namespace
{

// PSPSDK psprtc.h: ScePspDateTime (16 bytes):
//   +0x00 u16 year, +0x02 u16 month (1..12), +0x04 u16 day (1..31),
//   +0x06 u16 hour (0..23), +0x08 u16 minute (0..59), +0x0A u16 second (0..59),
//   +0x0C u32 microsecond (0..999999).
// PSPSDK psprtc.h: SCE_RTC_ERROR_INVALID_POINTER = 0x80510002.
constexpr int32_t kSceRtcErrorInvalidPointer = static_cast<int32_t>(0x80510002u);

void WritePspDateTime(uint32_t out_addr, const std::tm& tm_val, uint32_t usec)
{
    auto& mem = runtime::GetGuestMemory();
    mem.WriteU16(out_addr + 0x00u, static_cast<uint16_t>(tm_val.tm_year + 1900));
    mem.WriteU16(out_addr + 0x02u, static_cast<uint16_t>(tm_val.tm_mon + 1));
    mem.WriteU16(out_addr + 0x04u, static_cast<uint16_t>(tm_val.tm_mday));
    mem.WriteU16(out_addr + 0x06u, static_cast<uint16_t>(tm_val.tm_hour));
    mem.WriteU16(out_addr + 0x08u, static_cast<uint16_t>(tm_val.tm_min));
    mem.WriteU16(out_addr + 0x0Au, static_cast<uint16_t>(tm_val.tm_sec));
    mem.WriteU32(out_addr + 0x0Cu, usec);
}

// PSPSDK psprtc.h: int sceRtcGetCurrentClockLocalTime(pspTime *time);
void HleRtcGetCurrentClockLocalTime()
{
    const uint32_t out_addr = recomp::Arg<uint32_t>(0);
    if (out_addr == 0)
    {
        recomp::Return(kSceRtcErrorInvalidPointer);
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto usec = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - secs).count());
    const std::time_t tt = std::chrono::system_clock::to_time_t(secs);

    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif
    WritePspDateTime(out_addr, local_tm, usec);
    recomp::Return(kSceKernelErrorOk);
}

} // namespace

void RegisterRtc()
{
    auto& reg = runtime::GetNidRegistry();
    reg.RegisterByName("sceRtc", "sceRtcGetCurrentClockLocalTime", HleRtcGetCurrentClockLocalTime);
}

} // namespace repsp::hle::kernel::detail
