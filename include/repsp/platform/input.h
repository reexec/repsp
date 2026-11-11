#ifndef REPSP_PLATFORM_INPUT_H
#define REPSP_PLATFORM_INPUT_H

#include <cstdint>

namespace repsp::platform
{

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
