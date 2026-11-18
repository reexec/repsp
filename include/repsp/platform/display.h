#ifndef REPSP_PLATFORM_DISPLAY_H
#define REPSP_PLATFORM_DISPLAY_H

#include <cstdint>
#include <functional>
#include <string>

namespace repsp::platform
{

enum class VsyncMode
{
    kOff = 0,
    kOn = 1,
    kAdaptive = 2,
};

enum class AspectMode
{
    kOriginal = 0,
    kStretch = 1,
    kInteger = 2,
};

enum class OutputFilter
{
    kNearest = 0,
    kLinear = 1,
};

struct OutputRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

constexpr OutputRect ComputeOutputRect(int win_w, int win_h, int src_w, int src_h, AspectMode mode) noexcept
{
    if (win_w <= 0 || win_h <= 0 || src_w <= 0 || src_h <= 0)
    {
        return {0, 0, win_w > 0 ? win_w : 0, win_h > 0 ? win_h : 0};
    }
    if (mode == AspectMode::kStretch)
    {
        return {0, 0, win_w, win_h};
    }
    if (mode == AspectMode::kInteger)
    {
        const int kx = win_w / src_w;
        const int ky = win_h / src_h;
        const int k = kx < ky ? kx : ky;
        if (k >= 1)
        {
            const int w = src_w * k;
            const int h = src_h * k;
            return {(win_w - w) / 2, (win_h - h) / 2, w, h};
        }
    }
    // Original aspect (also fallback when window is smaller than 1x integer scale).
    const int64_t lhs = static_cast<int64_t>(win_w) * static_cast<int64_t>(src_h);
    const int64_t rhs = static_cast<int64_t>(src_w) * static_cast<int64_t>(win_h);
    if (lhs > rhs)
    {
        const int w = static_cast<int>((static_cast<int64_t>(win_h) * src_w + src_h / 2) / src_h);
        return {(win_w - w) / 2, 0, w, win_h};
    }
    const int h = static_cast<int>((static_cast<int64_t>(win_w) * src_h + src_w / 2) / src_w);
    return {0, (win_h - h) / 2, win_w, h};
}

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
    VsyncMode vsync_mode = VsyncMode::kOff;
    AspectMode aspect_mode = AspectMode::kOriginal;
    OutputFilter output_filter = OutputFilter::kNearest;
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

enum class ShortcutKey
{
    kOther = 0,
    kTab,
    kEnter,
    kDigit1,
    kDigit2,
    kDigit3,
    kDigit4,
    kDigit5,
    kDigit6,
    kDigit7,
    kDigit8,
};

enum class ShortcutKind
{
    kNone = 0,
    kToggleOverlay,
    kWindowScale,
    kToggleFullscreen,
    kRenderScale,
};

struct ShortcutResult
{
    ShortcutKind kind = ShortcutKind::kNone;
    int value = 0;
};

constexpr bool ShouldBlockGameKeyboardInput(bool overlay_visible, bool alt_held, bool ctrl_held) noexcept
{
    return overlay_visible || alt_held || ctrl_held;
}

constexpr ShortcutResult ClassifyShortcut(ShortcutKey key, bool alt_held, bool ctrl_held, bool repeat) noexcept
{
    if (repeat)
    {
        return {};
    }
    if (key == ShortcutKey::kTab && !alt_held && !ctrl_held)
    {
        return {ShortcutKind::kToggleOverlay, 0};
    }
    if (alt_held && !ctrl_held)
    {
        if (key >= ShortcutKey::kDigit1 && key <= ShortcutKey::kDigit6)
        {
            return {ShortcutKind::kWindowScale, 1 + static_cast<int>(key) - static_cast<int>(ShortcutKey::kDigit1)};
        }
        if (key == ShortcutKey::kEnter)
        {
            return {ShortcutKind::kToggleFullscreen, 0};
        }
    }
    if (ctrl_held && !alt_held)
    {
        if (key >= ShortcutKey::kDigit1 && key <= ShortcutKey::kDigit8)
        {
            return {ShortcutKind::kRenderScale, 1 + static_cast<int>(key) - static_cast<int>(ShortcutKey::kDigit1)};
        }
    }
    return {};
}

// Sets the leading part of the window title (app name/game ID). Safe to call
// before display initialization or from any thread; applied on the window's thread.
void SetDisplayTitle(const std::string& title);

// Registers a callback invoked on the UI thread when Ctrl+1..8 requests a new
// internal rendering scale (1..8).
void SetRenderScaleCallback(std::function<void(int)> callback);

// Runtime display options and telemetry.
void SetWindowScale(int scale);
void SetFullscreen(bool fullscreen);
bool IsFullscreen();
void SetVsyncMode(VsyncMode mode);
int GetSwapInterval();
void SetAspectMode(AspectMode mode);
AspectMode GetAspectMode();
void SetOutputFilter(OutputFilter filter);
OutputFilter GetOutputFilter();
double GetDisplayFps();

struct DisplayGlInfo
{
    bool valid = false;
    int major = 0;
    int minor = 0;
    std::string renderer;
    std::string vendor;
};

DisplayGlInfo GetDisplayGlInfo();
uint64_t GetProcessMemoryBytes();

} // namespace repsp::platform

#endif // REPSP_PLATFORM_DISPLAY_H
