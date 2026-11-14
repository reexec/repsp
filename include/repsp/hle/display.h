#ifndef REPSP_HLE_DISPLAY_H
#define REPSP_HLE_DISPLAY_H

#include <cstdint>
#include <string>

namespace repsp::hle::display
{

struct DisplayHleConfig
{
    bool headless = false;
    int scale = 2;
    std::string window_title = "repsp";
    std::string frame_dump_dir;
    uint32_t frame_dump_limit = 0;
};

struct DisplayStats
{
    int32_t mode = 0;
    int32_t width = 480;
    int32_t height = 272;
    uint32_t active_topaddr = 0;
    uint32_t active_bufferwidth = 0;
    int32_t active_pixelformat = 3;
    uint32_t vblank_count = 0;
    uint64_t frames_presented = 0;
    uint64_t frames_dumped = 0;
};

void Register(const DisplayHleConfig& config = DisplayHleConfig{});
void Shutdown();

DisplayStats GetStats();
std::string FormatStats();

// Convert a PSP framebuffer (formats 0..3: 565, 5551, 4444, 8888) in GuestMemory
// into 480x272 RGBA8 bytes (out_rgba must hold at least 480*272*4 bytes).
void ConvertFrameBufferToRgba8(uint32_t topaddr, uint32_t bufferwidth, int32_t pixelformat, uint8_t* out_rgba);

} // namespace repsp::hle::display

#endif // REPSP_HLE_DISPLAY_H
