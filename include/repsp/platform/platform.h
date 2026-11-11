#ifndef REPSP_PLATFORM_PLATFORM_H
#define REPSP_PLATFORM_PLATFORM_H

#include "repsp/platform/audio.h"
#include "repsp/platform/display.h"
#include "repsp/platform/input.h"

namespace repsp::platform
{

struct PlatformConfig
{
    DisplayConfig display;
    AudioConfig audio;
    bool headless = false;
};

bool Initialize(const PlatformConfig& config);
void Shutdown();
DisplayBackend* GetDisplay();
InputBackend* GetInput();
AudioBackend* GetAudio();

} // namespace repsp::platform

#endif // REPSP_PLATFORM_PLATFORM_H
