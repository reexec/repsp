#ifndef REPSP_PLATFORM_INPUT_H
#define REPSP_PLATFORM_INPUT_H

#include <cstdint>

namespace repsp::platform
{

// PSP button bitmasks from PSPSDK pspctrl.h (enum PspCtrlButtons).
constexpr uint32_t kPspCtrlSelect = 0x000001u;
constexpr uint32_t kPspCtrlStart = 0x000008u;
constexpr uint32_t kPspCtrlUp = 0x000010u;
constexpr uint32_t kPspCtrlRight = 0x000020u;
constexpr uint32_t kPspCtrlDown = 0x000040u;
constexpr uint32_t kPspCtrlLeft = 0x000080u;
constexpr uint32_t kPspCtrlLTrigger = 0x000100u;
constexpr uint32_t kPspCtrlRTrigger = 0x000200u;
constexpr uint32_t kPspCtrlTriangle = 0x001000u;
constexpr uint32_t kPspCtrlCircle = 0x002000u;
constexpr uint32_t kPspCtrlCross = 0x004000u;
constexpr uint32_t kPspCtrlSquare = 0x008000u;

struct ControllerState
{
    uint32_t buttons = 0;
    float lx = 0.0f;
    float ly = 0.0f;
};

class InputBackend
{
public:
    virtual ~InputBackend() = default;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual void Poll(ControllerState& state) = 0;
};

} // namespace repsp::platform

#endif // REPSP_PLATFORM_INPUT_H
