#ifndef REPSP_HLE_UMD_H
#define REPSP_HLE_UMD_H

#include <cstdint>
#include <filesystem>
#include <string>

namespace repsp::hle::umd
{

// Drive status bits from PSPSDK pspumd.h (pspUmdState).
constexpr uint32_t kUmdNotPresent = 0x01u; // PSP_UMD_NOT_PRESENT
constexpr uint32_t kUmdPresent = 0x02u;    // PSP_UMD_PRESENT
constexpr uint32_t kUmdChanged = 0x04u;    // PSP_UMD_CHANGED
constexpr uint32_t kUmdIniting = 0x08u;    // PSP_UMD_INITING
constexpr uint32_t kUmdInited = 0x10u;     // PSP_UMD_INITED
constexpr uint32_t kUmdReady = 0x20u;      // PSP_UMD_READY

constexpr uint32_t kUmdDefaultReadyStat = kUmdPresent | kUmdInited | kUmdReady; // 0x32
constexpr uint32_t kDefaultReadyDelayMs = 500u;

struct SwapResult
{
    bool ok = false;
    std::string reason;
};

// Host API for UMD drive state and disc replacement. Safe to call from host/UI threads.
bool IsReplacePermitted();
uint32_t GetDriveStat();
SwapResult EjectDisc();
SwapResult InsertDisc(const std::filesystem::path& disc_root);

// Configurable delay between InsertDisc (PRESENT|CHANGED) and READY (0x32).
// Hardware spin-up timing has no sourced constant; defaults to 500 ms.
void SetReadyDelayMs(uint32_t delay_ms);
uint32_t GetReadyDelayMs();

} // namespace repsp::hle::umd

#endif // REPSP_HLE_UMD_H
