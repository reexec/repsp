#ifndef REPSP_HLE_CTRL_H
#define REPSP_HLE_CTRL_H

#include "repsp/platform/input.h"

#include <cstdint>
#include <functional>
#include <string>

namespace repsp::hle::ctrl
{

constexpr int32_t kPspCtrlModeDigital = 0;
constexpr int32_t kPspCtrlModeAnalog = 1;

struct SceCtrlData
{
    uint32_t time_stamp = 0;
    uint32_t buttons = 0;
    uint8_t lx = 128;
    uint8_t ly = 128;
    uint8_t rsrv[6] = {0, 0, 0, 0, 0, 0};
};
static_assert(sizeof(SceCtrlData) == 16, "SceCtrlData must be 16 bytes");

struct CtrlStats
{
    int32_t sampling_cycle = 0;
    int32_t sampling_mode = kPspCtrlModeDigital;
    uint64_t samples_produced = 0;
    uint64_t samples_read = 0;
};

using InputSourceFn = std::function<platform::ControllerState(uint32_t vcount)>;

void Register();
void Shutdown();

// Override the input source for deterministic testing (pass nullptr to restore platform polling).
void SetInputSource(InputSourceFn fn);

CtrlStats GetStats();
std::string FormatStats();

} // namespace repsp::hle::ctrl

#endif // REPSP_HLE_CTRL_H
