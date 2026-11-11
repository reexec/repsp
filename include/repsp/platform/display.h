#ifndef REPSP_PLATFORM_DISPLAY_H
#define REPSP_PLATFORM_DISPLAY_H

#include <cstdint>
#include <string>

namespace repsp::platform
{

struct DisplayConfig
{
    int32_t width = 480;
    int32_t height = 272;
    std::string title = "repsp";
    bool fullscreen = false;
    bool dummy = false;
};

class DisplayBackend
{
public:
    virtual ~DisplayBackend() = default;
    virtual bool Initialize(const DisplayConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual void SwapBuffers() = 0;
    virtual bool PollEvents() = 0;
};

} // namespace repsp::platform

#endif // REPSP_PLATFORM_DISPLAY_H
