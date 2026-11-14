#ifndef REPSP_PLATFORM_DISPLAY_H
#define REPSP_PLATFORM_DISPLAY_H

#include <cstdint>
#include <functional>
#include <string>

namespace repsp::platform
{

struct DisplayConfig
{
    int32_t width = 960;
    int32_t height = 544;
    std::string title = "repsp";
    bool fullscreen = false;
    bool dummy = false;
    bool use_opengl = false;
    bool hidden = false;
    bool vsync = false;
};

class DisplayBackend
{
public:
    virtual ~DisplayBackend() = default;
    virtual bool Initialize(const DisplayConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual void PresentRgba8(const uint8_t* pixels, int width, int height, int pitch_bytes) = 0;
    virtual void SwapBuffers() = 0;
    virtual bool PollEvents() = 0;

    virtual bool HasGlContext() const { return false; }
    virtual void SubmitRenderJob(std::function<void()> job)
    {
        if (job)
        {
            job();
        }
    }
    virtual void WaitRenderIdle() {}
    virtual void* GetGlProcAddress(const char* name) const
    {
        (void)name;
        return nullptr;
    }
    virtual void PresentGlTexture(uint32_t texture_id, int width, int height)
    {
        (void)texture_id;
        (void)width;
        (void)height;
    }
};

} // namespace repsp::platform

#endif // REPSP_PLATFORM_DISPLAY_H
