#include "repsp/hle/display.h"
#include "repsp/gpu/render_backend.h"
#include "repsp/platform/display.h"
#include "repsp/platform/platform.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"
#include "repsp/runtime/scheduler.h"
#include "repsp/runtime/vblank_clock.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <vector>

namespace repsp::hle::display
{

namespace
{

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 272;
constexpr float kPspFps = 59.94005966f;

struct FrameBufferConfig
{
    uint32_t topaddr = 0;
    uint32_t bufferwidth = 0;
    int32_t pixelformat = 3;
};

std::mutex g_disp_mutex;
bool g_initialized = false;
uint32_t g_vblank_sub_id = 0;

DisplayHleConfig g_config;
platform::DisplayBackend* g_backend = nullptr;
bool g_owns_platform = false;
int32_t g_mode = 0;
int32_t g_width = kScreenWidth;
int32_t g_height = kScreenHeight;
FrameBufferConfig g_active_fb;
FrameBufferConfig g_pending_fb;
bool g_has_pending_fb = false;
uint64_t g_frames_presented = 0;
uint64_t g_frames_dumped = 0;
std::vector<uint8_t> g_rgba_scratch;

inline uint8_t Expand5(uint32_t v)
{
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}

inline uint8_t Expand6(uint32_t v)
{
    return static_cast<uint8_t>((v << 2) | (v >> 4));
}

inline uint8_t Expand4(uint32_t v)
{
    return static_cast<uint8_t>((v << 4) | v);
}

void WriteBmp(const std::filesystem::path& path, const uint8_t* rgba, int width, int height)
{
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f)
    {
        return;
    }

    const uint32_t pixel_bytes = static_cast<uint32_t>(width * height * 4);
    const uint32_t file_size = 54u + pixel_bytes;
    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(header + 2, &file_size, 4);
    const uint32_t data_offset = 54u;
    std::memcpy(header + 10, &data_offset, 4);
    const uint32_t dib_size = 40u;
    std::memcpy(header + 14, &dib_size, 4);
    const int32_t w = width;
    const int32_t h = -height; // top-down
    std::memcpy(header + 18, &w, 4);
    std::memcpy(header + 22, &h, 4);
    const uint16_t planes = 1;
    const uint16_t bpp = 32;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &pixel_bytes, 4);

    std::fwrite(header, 1, sizeof(header), f);

    std::vector<uint8_t> bgra(pixel_bytes);
    for (int i = 0; i < width * height; ++i)
    {
        bgra[i * 4 + 0] = rgba[i * 4 + 2]; // B
        bgra[i * 4 + 1] = rgba[i * 4 + 1]; // G
        bgra[i * 4 + 2] = rgba[i * 4 + 0]; // R
        bgra[i * 4 + 3] = 255;             // A
    }
    std::fwrite(bgra.data(), 1, bgra.size(), f);
    std::fclose(f);
}

void PresentActiveLocked()
{
    const bool need_dump = !g_config.frame_dump_dir.empty() &&
                           (g_config.frame_dump_limit == 0 || g_frames_dumped < g_config.frame_dump_limit);
    const bool is_gl = (gpu::GetRenderBackend() == &gpu::kOpenGlBackend) &&
                       gpu::HasGlTargetForAddress(g_active_fb.topaddr);
    if (is_gl)
    {
        gpu::PresentGlTarget(g_active_fb.topaddr);
        ++g_frames_presented;

        if (need_dump)
        {
            std::vector<uint8_t> rgba;
            int w = 0;
            int h = 0;
            if (gpu::ReadGlTargetRgba8(g_active_fb.topaddr, 1, rgba, w, h) &&
                w == kScreenWidth && h == kScreenHeight)
            {
                std::error_code ec;
                std::filesystem::create_directories(g_config.frame_dump_dir, ec);
                char name[32];
                std::snprintf(name, sizeof(name), "frame_%04llu.bmp",
                              static_cast<unsigned long long>(g_frames_dumped));
                WriteBmp(std::filesystem::path(g_config.frame_dump_dir) / name,
                         rgba.data(), kScreenWidth, kScreenHeight);
                ++g_frames_dumped;
            }
        }
        return;
    }

    g_rgba_scratch.resize(static_cast<size_t>(kScreenWidth) * static_cast<size_t>(kScreenHeight) * 4u);
    ConvertFrameBufferToRgba8(g_active_fb.topaddr, g_active_fb.bufferwidth,
                               g_active_fb.pixelformat, g_rgba_scratch.data());

    if (g_backend)
    {
        g_backend->PresentRgba8(g_rgba_scratch.data(), kScreenWidth, kScreenHeight, kScreenWidth * 4);
    }
    ++g_frames_presented;

    if (need_dump)
    {
        std::error_code ec;
        std::filesystem::create_directories(g_config.frame_dump_dir, ec);
        char name[32];
        std::snprintf(name, sizeof(name), "frame_%04llu.bmp",
                      static_cast<unsigned long long>(g_frames_dumped));
        WriteBmp(std::filesystem::path(g_config.frame_dump_dir) / name,
                 g_rgba_scratch.data(), kScreenWidth, kScreenHeight);
        ++g_frames_dumped;
    }
}

void OnVblankTick()
{
    bool close_requested = false;
    {
        std::lock_guard<std::mutex> lock(g_disp_mutex);
        if (!g_initialized)
        {
            return;
        }
        if (g_has_pending_fb)
        {
            g_active_fb = g_pending_fb;
            g_has_pending_fb = false;
            PresentActiveLocked();
        }
        if (g_backend && !g_backend->PollEvents())
        {
            close_requested = true;
        }
    }

    if (close_requested)
    {
        runtime::GetScheduler().RequestStop("window closed");
    }
}

void HleSetMode()
{
    const int32_t mode = static_cast<int32_t>(recomp::Arg<uint32_t>(0));
    const int32_t width = static_cast<int32_t>(recomp::Arg<uint32_t>(1));
    const int32_t height = static_cast<int32_t>(recomp::Arg<uint32_t>(2));
    {
        std::lock_guard<std::mutex> lock(g_disp_mutex);
        g_mode = mode;
        g_width = (width > 0) ? width : kScreenWidth;
        g_height = (height > 0) ? height : kScreenHeight;
    }
    recomp::Return(0);
}

void HleSetFrameBuf()
{
    const uint32_t topaddr = recomp::Arg<uint32_t>(0);
    const uint32_t bufferwidth = recomp::Arg<uint32_t>(1);
    const int32_t pixelformat = static_cast<int32_t>(recomp::Arg<uint32_t>(2));
    const int32_t sync = static_cast<int32_t>(recomp::Arg<uint32_t>(3));

    std::lock_guard<std::mutex> lock(g_disp_mutex);
    const FrameBufferConfig fb{topaddr, bufferwidth, pixelformat};
    if (sync == 0)
    {
        g_active_fb = fb;
        g_has_pending_fb = false;
        PresentActiveLocked();
    }
    else
    {
        g_pending_fb = fb;
        g_has_pending_fb = true;
    }
    recomp::Return(0);
}

void WaitVblankImpl(bool allow_callbacks)
{
    recomp::Return(runtime::GetVblankClock().WaitNext(allow_callbacks));
}

void HleWaitVblankStart()
{
    WaitVblankImpl(false);
}

void HleWaitVblankStartCB()
{
    WaitVblankImpl(true);
}

void HleGetVcount()
{
    recomp::Return(runtime::GetVblankClock().Count());
}

void HleGetFramePerSec()
{
    recomp::Return(kPspFps);
}

} // namespace

void ConvertFrameBufferToRgba8(uint32_t topaddr, uint32_t bufferwidth, int32_t pixelformat, uint8_t* out_rgba)
{
    if (!out_rgba)
    {
        return;
    }
    if (topaddr == 0 || bufferwidth == 0)
    {
        const size_t total = static_cast<size_t>(kScreenWidth) * static_cast<size_t>(kScreenHeight);
        for (size_t i = 0; i < total; ++i)
        {
            out_rgba[i * 4 + 0] = 0;
            out_rgba[i * 4 + 1] = 0;
            out_rgba[i * 4 + 2] = 0;
            out_rgba[i * 4 + 3] = 255;
        }
        return;
    }

    auto& mem = runtime::GetGuestMemory();
    const uint32_t bpp = (pixelformat == 3) ? 4u : 2u;

    for (int y = 0; y < kScreenHeight; ++y)
    {
        const uint32_t row_addr = topaddr + static_cast<uint32_t>(y) * bufferwidth * bpp;
        uint8_t* dst = out_rgba + static_cast<size_t>(y) * static_cast<size_t>(kScreenWidth) * 4u;
        for (int x = 0; x < kScreenWidth; ++x)
        {
            const uint32_t addr = row_addr + static_cast<uint32_t>(x) * bpp;
            if (!mem.IsValidAddress(addr, bpp))
            {
                dst[x * 4 + 0] = 0;
                dst[x * 4 + 1] = 0;
                dst[x * 4 + 2] = 0;
                dst[x * 4 + 3] = 255;
                continue;
            }

            if (pixelformat == 3)
            {
                const uint32_t px = mem.ReadU32(addr);
                dst[x * 4 + 0] = static_cast<uint8_t>(px & 0xFFu);
                dst[x * 4 + 1] = static_cast<uint8_t>((px >> 8) & 0xFFu);
                dst[x * 4 + 2] = static_cast<uint8_t>((px >> 16) & 0xFFu);
                dst[x * 4 + 3] = 255;
            }
            else if (pixelformat == 0)
            {
                const uint16_t px = mem.ReadU16(addr);
                dst[x * 4 + 0] = Expand5(px & 0x1Fu);
                dst[x * 4 + 1] = Expand6((px >> 5) & 0x3Fu);
                dst[x * 4 + 2] = Expand5((px >> 11) & 0x1Fu);
                dst[x * 4 + 3] = 255;
            }
            else if (pixelformat == 1)
            {
                const uint16_t px = mem.ReadU16(addr);
                dst[x * 4 + 0] = Expand5(px & 0x1Fu);
                dst[x * 4 + 1] = Expand5((px >> 5) & 0x1Fu);
                dst[x * 4 + 2] = Expand5((px >> 10) & 0x1Fu);
                dst[x * 4 + 3] = (px & 0x8000u) ? 255 : 0;
            }
            else
            {
                const uint16_t px = mem.ReadU16(addr);
                dst[x * 4 + 0] = Expand4(px & 0xFu);
                dst[x * 4 + 1] = Expand4((px >> 4) & 0xFu);
                dst[x * 4 + 2] = Expand4((px >> 8) & 0xFu);
                dst[x * 4 + 3] = Expand4((px >> 12) & 0xFu);
            }
        }
    }
}

void Register(const DisplayHleConfig& config)
{
    Shutdown();

    {
        std::lock_guard<std::mutex> lock(g_disp_mutex);
        g_config = config;
        if (g_config.frame_dump_dir.empty())
        {
            if (const char* env_dir = std::getenv("REPSP_FRAME_DUMP"))
            {
                g_config.frame_dump_dir = env_dir;
            }
        }
        if (g_config.frame_dump_limit == 0)
        {
            if (const char* env_lim = std::getenv("REPSP_FRAME_DUMP_LIMIT"))
            {
                const long lim = std::atol(env_lim);
                if (lim > 0)
                {
                    g_config.frame_dump_limit = static_cast<uint32_t>(lim);
                }
            }
        }
        if (g_config.scale <= 0)
        {
            g_config.scale = 2;
        }

        g_mode = 0;
        g_width = kScreenWidth;
        g_height = kScreenHeight;
        g_active_fb = FrameBufferConfig{};
        g_pending_fb = FrameBufferConfig{};
        g_has_pending_fb = false;
        g_frames_presented = 0;
        g_frames_dumped = 0;

        g_backend = platform::GetDisplay();
        g_owns_platform = false;
        if (g_backend == nullptr)
        {
            platform::PlatformConfig pc;
            pc.headless = g_config.headless && !g_config.use_opengl;
            pc.display.width = kScreenWidth * g_config.scale;
            pc.display.height = kScreenHeight * g_config.scale;
            pc.display.title = g_config.window_title;
            pc.display.dummy = g_config.headless && !g_config.use_opengl;
            pc.display.use_opengl = g_config.use_opengl;
            pc.display.hidden = g_config.headless && g_config.use_opengl;
            pc.display.vsync = g_config.vsync;
            pc.audio.dummy = true;
            if (!platform::Initialize(pc) && (!g_config.headless || g_config.use_opengl))
            {
                runtime::Log(runtime::LogLevel::kWarn, "[DISPLAY] Failed to initialize display backend, falling back to headless");
                pc.headless = true;
                pc.display.dummy = true;
                pc.display.use_opengl = false;
                pc.display.hidden = false;
                platform::Initialize(pc);
            }
            g_backend = platform::GetDisplay();
            g_owns_platform = true;
        }

        if (g_config.use_opengl)
        {
            gpu::SetInternalScale(g_config.internal_scale > 0 ? g_config.internal_scale : 1);
            if (g_backend && g_backend->HasGlContext())
            {
                gpu::SelectRenderBackend("opengl");
            }
        }

        g_initialized = true;
        g_vblank_sub_id = runtime::GetVblankClock().Subscribe([]() { OnVblankTick(); });
    }

    runtime::GetVblankClock().Start();

    auto& reg = runtime::GetNidRegistry();
    reg.Register("sceDisplay", 0x0E20F177u, "sceDisplaySetMode", HleSetMode);
    reg.Register("sceDisplay", 0x289D82FEu, "sceDisplaySetFrameBuf", HleSetFrameBuf);
    reg.Register("sceDisplay", 0x984C27E7u, "sceDisplayWaitVblankStart", HleWaitVblankStart);
    reg.Register("sceDisplay", 0x46F186C3u, "sceDisplayWaitVblankStartCB", HleWaitVblankStartCB);
    reg.Register("sceDisplay", 0x9C6EAAD7u, "sceDisplayGetVcount", HleGetVcount);
    reg.Register("sceDisplay", 0xDBA6C4C4u, "sceDisplayGetFramePerSec", HleGetFramePerSec);
}

void Shutdown()
{
    uint32_t sub_id = 0;
    bool owns_platform = false;
    bool was_initialized = false;
    {
        std::lock_guard<std::mutex> lock(g_disp_mutex);
        if (!g_initialized)
        {
            return;
        }
        was_initialized = true;
        g_initialized = false;
        sub_id = g_vblank_sub_id;
        g_vblank_sub_id = 0;
        owns_platform = g_owns_platform;
        g_owns_platform = false;
        g_backend = nullptr;
    }

    if (was_initialized)
    {
        runtime::GetVblankClock().Unsubscribe(sub_id);
        runtime::GetVblankClock().Stop();
    }
    if (owns_platform)
    {
        platform::Shutdown();
    }
}

DisplayStats GetStats()
{
    std::lock_guard<std::mutex> lock(g_disp_mutex);
    DisplayStats s;
    s.mode = g_mode;
    s.width = g_width;
    s.height = g_height;
    s.active_topaddr = g_active_fb.topaddr;
    s.active_bufferwidth = g_active_fb.bufferwidth;
    s.active_pixelformat = g_active_fb.pixelformat;
    s.vblank_count = runtime::GetVblankClock().Count();
    s.frames_presented = g_frames_presented;
    s.frames_dumped = g_frames_dumped;
    return s;
}

std::string FormatStats()
{
    const DisplayStats s = GetStats();
    std::ostringstream oss;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Display: mode=%d %dx%d fb=0x%08X stride=%u fmt=%d vblanks=%u presented=%llu dumped=%llu\n",
                  s.mode, s.width, s.height, s.active_topaddr, s.active_bufferwidth, s.active_pixelformat,
                  s.vblank_count, static_cast<unsigned long long>(s.frames_presented),
                  static_cast<unsigned long long>(s.frames_dumped));
    oss << buf;
    return oss.str();
}

} // namespace repsp::hle::display
