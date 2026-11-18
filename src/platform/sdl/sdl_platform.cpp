#include "repsp/platform/platform.h"
#include "repsp/platform/overlay.h"
#include "repsp/platform/post_shader.h"

#include "display_sdl_internal.h"
#include "opengl_post_process.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace repsp::platform
{

namespace
{

struct InputMappingEntry
{
    uint32_t psp_bit;
    SDL_Scancode scancode;
    SDL_GamepadButton gamepad_button;
};

// Single unified keyboard/gamepad -> PSP button mapping table.
constexpr InputMappingEntry kButtonMappings[] = {
    {kPspCtrlUp, SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {kPspCtrlRight, SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {kPspCtrlDown, SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {kPspCtrlLeft, SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {kPspCtrlCross, SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_SOUTH},
    {kPspCtrlCircle, SDL_SCANCODE_X, SDL_GAMEPAD_BUTTON_EAST},
    {kPspCtrlSquare, SDL_SCANCODE_A, SDL_GAMEPAD_BUTTON_WEST},
    {kPspCtrlTriangle, SDL_SCANCODE_S, SDL_GAMEPAD_BUTTON_NORTH},
    {kPspCtrlLTrigger, SDL_SCANCODE_Q, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {kPspCtrlRTrigger, SDL_SCANCODE_W, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {kPspCtrlStart, SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START},
    {kPspCtrlSelect, SDL_SCANCODE_BACKSPACE, SDL_GAMEPAD_BUTTON_BACK},
};

std::mutex g_input_mutex;
ControllerState g_input_snapshot;
bool g_input_dummy = true;

// Owned and touched exclusively on SdlDisplayBackend's UI thread.
SDL_Window* g_overlay_window = nullptr;
SDL_GLContext g_overlay_gl_ctx = nullptr;

std::mutex g_title_mutex;
std::string g_app_title;
std::atomic<bool> g_title_dirty{false};

std::mutex g_render_scale_mutex;
std::function<void(int)> g_render_scale_cb;

std::atomic<int> g_aspect_mode{static_cast<int>(AspectMode::kOriginal)};
std::atomic<int> g_output_filter{static_cast<int>(OutputFilter::kNearest)};
std::atomic<int> g_vsync_mode{static_cast<int>(VsyncMode::kOff)};
std::atomic<int> g_swap_interval{0};
std::atomic<bool> g_fullscreen_state{false};
std::atomic<double> g_display_fps{0.0};

std::mutex g_gl_info_mutex;
DisplayGlInfo g_gl_info;

std::mutex g_display_req_mutex;
int g_req_window_scale = 0;
int g_req_fullscreen = -1;
int g_req_vsync = -1;

std::mutex g_post_shader_mutex;
std::filesystem::path g_post_shader_dir;
std::string g_initial_post_shader = kPostShaderNoneId;
std::atomic<sdl_internal::OpenGlPostProcess*> g_post_process{nullptr};

constexpr int32_t kPspBaseWidth = 480;
constexpr int32_t kPspBaseHeight = 272;

ShortcutKey MapSdlKeyToShortcutKey(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_TAB:
        return ShortcutKey::kTab;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return ShortcutKey::kEnter;
    case SDLK_1:
        return ShortcutKey::kDigit1;
    case SDLK_2:
        return ShortcutKey::kDigit2;
    case SDLK_3:
        return ShortcutKey::kDigit3;
    case SDLK_4:
        return ShortcutKey::kDigit4;
    case SDLK_5:
        return ShortcutKey::kDigit5;
    case SDLK_6:
        return ShortcutKey::kDigit6;
    case SDLK_7:
        return ShortcutKey::kDigit7;
    case SDLK_8:
        return ShortcutKey::kDigit8;
    default:
        return ShortcutKey::kOther;
    }
}

void ApplyWindowScale(SDL_Window* window, int scale)
{
    if (window == nullptr || scale < 1 || scale > 6)
    {
        return;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0)
    {
        SDL_SetWindowFullscreen(window, false);
    }
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0)
    {
        SDL_RestoreWindow(window);
    }
    SDL_SetWindowSize(window, kPspBaseWidth * scale, kPspBaseHeight * scale);
}

void ToggleFullscreen(SDL_Window* window)
{
    if (window == nullptr)
    {
        return;
    }
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0;
    if (SDL_SetWindowFullscreen(window, fullscreen))
    {
        g_fullscreen_state.store(fullscreen);
    }
}

int ApplySwapIntervalToCurrentContext(VsyncMode mode, bool hidden)
{
    if (hidden)
    {
        SDL_GL_SetSwapInterval(0);
        g_swap_interval.store(0);
        return 0;
    }
    int interval = 0;
    if (mode == VsyncMode::kAdaptive && SDL_GL_SetSwapInterval(-1))
    {
        interval = -1;
    }
    else if (mode != VsyncMode::kOff && SDL_GL_SetSwapInterval(1))
    {
        interval = 1;
    }
    else
    {
        SDL_GL_SetSwapInterval(0);
    }
    g_swap_interval.store(interval);
    return interval;
}

std::string FormatWindowTitle(const std::string& fallback, bool use_opengl, int gl_major, int gl_minor, double fps)
{
    std::string base;
    {
        std::lock_guard<std::mutex> lock(g_title_mutex);
        base = g_app_title.empty() ? fallback : g_app_title;
    }
    char suffix[96];
    if (use_opengl)
    {
        std::snprintf(suffix, sizeof(suffix), " - OpenGL %d.%d - FPS : %.1f", gl_major, gl_minor, fps);
    }
    else
    {
        std::snprintf(suffix, sizeof(suffix), " - Software - FPS : %.1f", fps);
    }
    return base + suffix;
}

float NormalizeSdlAxis(Sint16 raw)
{
    return (raw >= 0)
        ? (static_cast<float>(raw) / 32767.0f)
        : (static_cast<float>(raw) / 32768.0f);
}

void ResetInputSnapshot(bool dummy)
{
    std::lock_guard<std::mutex> lock(g_input_mutex);
    g_input_snapshot = ControllerState{};
    g_input_dummy = dummy;
}

void UpdateInputSnapshotFromSdl(SDL_Gamepad* gamepad)
{
    if (OverlayVisible())
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (!g_input_dummy)
        {
            g_input_snapshot = ControllerState{};
        }
        return;
    }

    const SDL_Keymod mod = SDL_GetModState();
    const bool block_kbd = ShouldBlockGameKeyboardInput(
        OverlayVisible(),
        (mod & SDL_KMOD_ALT) != 0,
        (mod & SDL_KMOD_CTRL) != 0);

    int num_keys = 0;
    const bool* keys = block_kbd ? nullptr : SDL_GetKeyboardState(&num_keys);

    ControllerState next;
    for (const auto& entry : kButtonMappings)
    {
        bool pressed = (keys && entry.scancode < num_keys && keys[entry.scancode]);
        if (!pressed && gamepad && SDL_GetGamepadButton(gamepad, entry.gamepad_button))
        {
            pressed = true;
        }
        if (pressed)
        {
            next.buttons |= entry.psp_bit;
        }
    }

    float lx = gamepad ? NormalizeSdlAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)) : 0.0f;
    float ly = gamepad ? NormalizeSdlAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)) : 0.0f;
    if (keys)
    {
        if (SDL_SCANCODE_J < num_keys && keys[SDL_SCANCODE_J]) lx -= 1.0f;
        if (SDL_SCANCODE_L < num_keys && keys[SDL_SCANCODE_L]) lx += 1.0f;
        if (SDL_SCANCODE_I < num_keys && keys[SDL_SCANCODE_I]) ly -= 1.0f;
        if (SDL_SCANCODE_K < num_keys && keys[SDL_SCANCODE_K]) ly += 1.0f;
    }
    next.lx = std::clamp(lx, -1.0f, 1.0f);
    next.ly = std::clamp(ly, -1.0f, 1.0f);

    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (!g_input_dummy)
    {
        g_input_snapshot = next;
    }
}

SDL_Gamepad* OpenFirstGamepad()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* pad = nullptr;
    if (ids && count > 0)
    {
        pad = SDL_OpenGamepad(ids[0]);
    }
    if (ids)
    {
        SDL_free(ids);
    }
    return pad;
}

#if defined(_WIN32) && !defined(_WIN64)
#define REPSP_SDL_GL_APIENTRY __stdcall
#else
#define REPSP_SDL_GL_APIENTRY
#endif

struct GlPresentHelper
{
    using GLenum = unsigned int;
    using GLuint = unsigned int;
    using GLint = int;
    using GLsizei = int;
    using GLboolean = unsigned char;
    using GLbitfield = unsigned int;
    using GLchar = char;

    static constexpr GLenum kGlTexture2d = 0x0DE1;
    static constexpr GLenum kGlTexture0 = 0x84C0;
    static constexpr GLenum kGlRgba = 0x1908;
    static constexpr GLenum kGlRgba8 = 0x8058;
    static constexpr GLenum kGlUnsignedByte = 0x1401;
    static constexpr GLenum kGlNearest = 0x2600;
    static constexpr GLenum kGlLinear = 0x2601;
    static constexpr GLenum kGlVendor = 0x1F00;
    static constexpr GLenum kGlRenderer = 0x1F01;
    static constexpr GLenum kGlClampToEdge = 0x812F;
    static constexpr GLenum kGlTextureMinFilter = 0x2801;
    static constexpr GLenum kGlTextureMagFilter = 0x2800;
    static constexpr GLenum kGlTextureWrapS = 0x2802;
    static constexpr GLenum kGlTextureWrapT = 0x2803;
    static constexpr GLenum kGlDepthTest = 0x0B71;
    static constexpr GLenum kGlScissorTest = 0x0C11;
    static constexpr GLenum kGlBlend = 0x0BE2;
    static constexpr GLenum kGlStencilTest = 0x0B90;
    static constexpr GLenum kGlCullFace = 0x0B44;
    static constexpr GLbitfield kGlColorBufferBit = 0x00004000;
    static constexpr GLenum kGlTriangleStrip = 0x0005;
    static constexpr GLenum kGlVertexShader = 0x8B31;
    static constexpr GLenum kGlFragmentShader = 0x8B30;
    static constexpr GLenum kGlCompileStatus = 0x8B81;
    static constexpr GLenum kGlLinkStatus = 0x8B82;
    static constexpr GLenum kGlDrawFramebuffer = 0x8CA9;
    static constexpr GLenum kGlUnpackAlignment = 0x0CF5;

    void (REPSP_SDL_GL_APIENTRY *glViewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glDisable)(GLenum) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glClearColor)(float, float, float, float) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glClear)(GLbitfield) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glBindFramebuffer)(GLenum, GLuint) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glUseProgram)(GLuint) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glBindVertexArray)(GLuint) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glActiveTexture)(GLenum) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glBindTexture)(GLenum, GLuint) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glTexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glGenTextures)(GLsizei, GLuint*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glDeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glDrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    GLuint (REPSP_SDL_GL_APIENTRY *glCreateShader)(GLenum) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glCompileShader)(GLuint) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glGetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glDeleteShader)(GLuint) = nullptr;
    GLuint (REPSP_SDL_GL_APIENTRY *glCreateProgram)() = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glAttachShader)(GLuint, GLuint) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glLinkProgram)(GLuint) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glGetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glDeleteProgram)(GLuint) = nullptr;
    GLint (REPSP_SDL_GL_APIENTRY *glGetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glUniform1i)(GLint, GLint) = nullptr;
    const unsigned char* (REPSP_SDL_GL_APIENTRY *glGetString)(GLenum) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glGenVertexArrays)(GLsizei, GLuint*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glDeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
    void (REPSP_SDL_GL_APIENTRY *glPixelStorei)(GLenum, GLint) = nullptr;

    GLuint program = 0;
    GLuint vao = 0;
    GLuint cpu_tex = 0;
    int cpu_tex_w = 0;
    int cpu_tex_h = 0;
    GLint u_tex = -1;
    GLint u_flip_y = -1;

    template <typename Fn>
    static bool LoadProc(Fn& fn, const char* name)
    {
        SDL_FunctionPointer sym = SDL_GL_GetProcAddress(name);
        if (sym == nullptr)
        {
            return false;
        }
        static_assert(sizeof(fn) == sizeof(sym));
        std::memcpy(&fn, &sym, sizeof(fn));
        return true;
    }

    GLuint CompileShader(GLenum type, const char* src) const
    {
        GLuint shader = glCreateShader(type);
        if (shader == 0)
        {
            return 0;
        }
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, kGlCompileStatus, &ok);
        if (!ok)
        {
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    bool Init()
    {
        if (!LoadProc(glViewport, "glViewport") ||
            !LoadProc(glDisable, "glDisable") ||
            !LoadProc(glClearColor, "glClearColor") ||
            !LoadProc(glClear, "glClear") ||
            !LoadProc(glBindFramebuffer, "glBindFramebuffer") ||
            !LoadProc(glUseProgram, "glUseProgram") ||
            !LoadProc(glBindVertexArray, "glBindVertexArray") ||
            !LoadProc(glActiveTexture, "glActiveTexture") ||
            !LoadProc(glBindTexture, "glBindTexture") ||
            !LoadProc(glTexImage2D, "glTexImage2D") ||
            !LoadProc(glTexSubImage2D, "glTexSubImage2D") ||
            !LoadProc(glTexParameteri, "glTexParameteri") ||
            !LoadProc(glGenTextures, "glGenTextures") ||
            !LoadProc(glDeleteTextures, "glDeleteTextures") ||
            !LoadProc(glDrawArrays, "glDrawArrays") ||
            !LoadProc(glCreateShader, "glCreateShader") ||
            !LoadProc(glShaderSource, "glShaderSource") ||
            !LoadProc(glCompileShader, "glCompileShader") ||
            !LoadProc(glGetShaderiv, "glGetShaderiv") ||
            !LoadProc(glDeleteShader, "glDeleteShader") ||
            !LoadProc(glCreateProgram, "glCreateProgram") ||
            !LoadProc(glAttachShader, "glAttachShader") ||
            !LoadProc(glLinkProgram, "glLinkProgram") ||
            !LoadProc(glGetProgramiv, "glGetProgramiv") ||
            !LoadProc(glDeleteProgram, "glDeleteProgram") ||
            !LoadProc(glGetUniformLocation, "glGetUniformLocation") ||
            !LoadProc(glUniform1i, "glUniform1i") ||
            !LoadProc(glGetString, "glGetString") ||
            !LoadProc(glGenVertexArrays, "glGenVertexArrays") ||
            !LoadProc(glDeleteVertexArrays, "glDeleteVertexArrays") ||
            !LoadProc(glPixelStorei, "glPixelStorei"))
        {
            return false;
        }

        const char* vs_src =
            "#version 330 core\n"
            "uniform int uFlipY;\n"
            "out vec2 vUv;\n"
            "void main() {\n"
            "    vec2 pos = vec2((gl_VertexID & 1) * 2 - 1, (gl_VertexID >> 1) * 2 - 1);\n"
            "    vUv = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
            "    if (uFlipY != 0) vUv.y = 1.0 - vUv.y;\n"
            "    gl_Position = vec4(pos, 0.0, 1.0);\n"
            "}\n";

        const char* fs_src =
            "#version 330 core\n"
            "uniform sampler2D uTex;\n"
            "in vec2 vUv;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    vec3 c = texture(uTex, vUv).rgb;\n"
            "    FragColor = vec4(c, 1.0);\n"
            "}\n";

        GLuint vs = CompileShader(kGlVertexShader, vs_src);
        GLuint fs = CompileShader(kGlFragmentShader, fs_src);
        if (vs == 0 || fs == 0)
        {
            if (vs != 0) glDeleteShader(vs);
            if (fs != 0) glDeleteShader(fs);
            return false;
        }

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint linked = 0;
        glGetProgramiv(program, kGlLinkStatus, &linked);
        if (!linked)
        {
            glDeleteProgram(program);
            program = 0;
            return false;
        }

        u_tex = glGetUniformLocation(program, "uTex");
        u_flip_y = glGetUniformLocation(program, "uFlipY");
        glGenVertexArrays(1, &vao);
        return vao != 0;
    }

    void Shutdown()
    {
        if (cpu_tex != 0 && glDeleteTextures != nullptr)
        {
            glDeleteTextures(1, &cpu_tex);
            cpu_tex = 0;
        }
        if (vao != 0 && glDeleteVertexArrays != nullptr)
        {
            glDeleteVertexArrays(1, &vao);
            vao = 0;
        }
        if (program != 0 && glDeleteProgram != nullptr)
        {
            glDeleteProgram(program);
            program = 0;
        }
    }

    void DrawTexture(GLuint tex, int src_w, int src_h, int win_w, int win_h, bool flip_y,
                     AspectMode aspect, OutputFilter filter,
                     sdl_internal::OpenGlPostProcess* post_process = nullptr) const
    {
        glBindFramebuffer(kGlDrawFramebuffer, 0);
        glViewport(0, 0, win_w, win_h);
        glDisable(kGlDepthTest);
        glDisable(kGlScissorTest);
        glDisable(kGlBlend);
        glDisable(kGlStencilTest);
        glDisable(kGlCullFace);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(kGlColorBufferBit);

        const OutputRect rect = ComputeOutputRect(win_w, win_h, src_w, src_h, aspect);
        if (rect.width <= 0 || rect.height <= 0)
        {
            return;
        }
        glViewport(rect.x, rect.y, rect.width, rect.height);

        const GLint gl_filter = (filter == OutputFilter::kLinear)
                                    ? static_cast<GLint>(kGlLinear)
                                    : static_cast<GLint>(kGlNearest);

        glActiveTexture(kGlTexture0);
        glBindTexture(kGlTexture2d, tex);
        glTexParameteri(kGlTexture2d, kGlTextureMinFilter, gl_filter);
        glTexParameteri(kGlTexture2d, kGlTextureMagFilter, gl_filter);
        glTexParameteri(kGlTexture2d, kGlTextureWrapS, kGlClampToEdge);
        glTexParameteri(kGlTexture2d, kGlTextureWrapT, kGlClampToEdge);

        if (post_process != nullptr && post_process->active() &&
            post_process->UseForPresent(
                tex,
                static_cast<std::uint32_t>(src_w),
                static_cast<std::uint32_t>(src_h),
                static_cast<std::uint32_t>(kPspBaseWidth),
                static_cast<std::uint32_t>(kPspBaseHeight),
                rect.x,
                rect.y,
                static_cast<std::uint32_t>(rect.width),
                static_cast<std::uint32_t>(rect.height),
                flip_y))
        {
            post_process->DrawLastQuad();
            glViewport(0, 0, win_w, win_h);
            return;
        }

        glUseProgram(program);
        glUniform1i(u_tex, 0);
        glUniform1i(u_flip_y, flip_y ? 1 : 0);
        glBindVertexArray(vao);
        glDrawArrays(kGlTriangleStrip, 0, 4);
        glBindVertexArray(0);
        glBindTexture(kGlTexture2d, 0);
        glUseProgram(0);
        glViewport(0, 0, win_w, win_h);
    }

    void DrawCpuRgba8(const uint8_t* pixels, int w, int h, int win_w, int win_h,
                      AspectMode aspect, OutputFilter filter,
                      sdl_internal::OpenGlPostProcess* post_process = nullptr)
    {
        if (cpu_tex == 0 || cpu_tex_w != w || cpu_tex_h != h)
        {
            if (cpu_tex == 0)
            {
                glGenTextures(1, &cpu_tex);
            }
            glActiveTexture(kGlTexture0);
            glBindTexture(kGlTexture2d, cpu_tex);
            glPixelStorei(kGlUnpackAlignment, 4);
            glTexImage2D(kGlTexture2d, 0, static_cast<GLint>(kGlRgba8), w, h, 0,
                         kGlRgba, kGlUnsignedByte, pixels);
            cpu_tex_w = w;
            cpu_tex_h = h;
        }
        else
        {
            glActiveTexture(kGlTexture0);
            glBindTexture(kGlTexture2d, cpu_tex);
            glPixelStorei(kGlUnpackAlignment, 4);
            glTexSubImage2D(kGlTexture2d, 0, 0, 0, w, h, kGlRgba, kGlUnsignedByte, pixels);
        }
        DrawTexture(cpu_tex, w, h, win_w, win_h, true, aspect, filter, post_process);
    }
};

class SdlDisplayBackend final : public DisplayBackend
{
public:
    SdlDisplayBackend() = default;
    ~SdlDisplayBackend() override
    {
        Shutdown();
    }

    bool Initialize(const DisplayConfig& config) override
    {
        Shutdown();
        config_ = config;
        close_requested_.store(false);

        if (config.dummy)
        {
            has_gl_context_.store(false);
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            {
                return false;
            }
            window_ = SDL_CreateWindow(config.title.c_str(), config.width, config.height, 0);
            initialized_ = true;
            return true;
        }

        // Run SDL video/window/event pump on a dedicated thread so the OS message
        // loop remains responsive while the main thread blocks in Scheduler::Run().
        has_gl_context_.store(false);
        ui_ready_ = false;
        ui_init_ok_ = false;
        ui_stop_.store(false);
        job_running_ = false;
        pending_presents_ = 0;
        gl_present_dirty_ = false;
        frame_dirty_ = false;
        ui_thread_ = std::thread([this]()
        {
            UiThreadMain();
        });

        std::unique_lock<std::mutex> lock(ui_mutex_);
        ui_cv_.wait(lock, [this]() { return ui_ready_; });
        if (!ui_init_ok_)
        {
            lock.unlock();
            if (ui_thread_.joinable())
            {
                ui_thread_.join();
            }
            return false;
        }
        initialized_ = true;
        return true;
    }

    void Shutdown() override
    {
        if (!initialized_)
        {
            return;
        }
        initialized_ = false;

        if (config_.dummy)
        {
            if (window_ != nullptr)
            {
                SDL_DestroyWindow(window_);
                window_ = nullptr;
            }
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return;
        }

        ui_stop_.store(true);
        ui_cv_.notify_all();
        if (ui_thread_.joinable())
        {
            ui_thread_.join();
        }
        has_gl_context_.store(false);
    }

    void PresentRgba8(const uint8_t* pixels, int width, int height, int pitch_bytes) override
    {
        if (config_.dummy || pixels == nullptr || width <= 0 || height <= 0 || pitch_bytes <= 0)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(ui_mutex_);
        const size_t dst_pitch = static_cast<size_t>(width) * 4u;
        frame_buf_.resize(dst_pitch * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(frame_buf_.data() + static_cast<size_t>(y) * dst_pitch,
                        pixels + static_cast<size_t>(y) * static_cast<size_t>(pitch_bytes),
                        dst_pitch);
        }
        frame_w_ = width;
        frame_h_ = height;
        frame_dirty_ = true;
        ui_cv_.notify_all();
    }

    void SwapBuffers() override
    {
    }

    bool PollEvents() override
    {
        if (config_.dummy)
        {
            return true;
        }
        return !close_requested_.load();
    }

    bool HasGlContext() const override
    {
        return has_gl_context_.load();
    }

    void SubmitRenderJob(std::function<void()> job) override
    {
        if (!job || config_.dummy || !has_gl_context_.load() || ui_stop_.load())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(ui_mutex_);
        job_queue_.push_back(std::move(job));
        ui_cv_.notify_all();
    }

    void WaitRenderIdle() override
    {
        if (config_.dummy || !has_gl_context_.load())
        {
            return;
        }
        std::unique_lock<std::mutex> lock(ui_mutex_);
        ui_cv_.wait(lock, [this]()
        {
            return (job_queue_.empty() && !job_running_) || ui_stop_.load();
        });
    }

    void* GetGlProcAddress(const char* name) const override
    {
        if (!has_gl_context_.load() || name == nullptr)
        {
            return nullptr;
        }
        SDL_FunctionPointer sym = SDL_GL_GetProcAddress(name);
        if (sym == nullptr)
        {
            return nullptr;
        }
        void* ptr = nullptr;
        static_assert(sizeof(ptr) == sizeof(sym));
        std::memcpy(&ptr, &sym, sizeof(ptr));
        return ptr;
    }

    void PresentGlTexture(uint32_t texture_id, int width, int height) override
    {
        if (config_.dummy || !has_gl_context_.load() || ui_stop_.load() ||
            texture_id == 0 || width <= 0 || height <= 0)
        {
            return;
        }
        std::unique_lock<std::mutex> lock(ui_mutex_);
        ui_cv_.wait(lock, [this]()
        {
            return pending_presents_ <= 1 || ui_stop_.load();
        });
        if (ui_stop_.load())
        {
            return;
        }
        gl_present_tex_ = texture_id;
        gl_present_w_ = width;
        gl_present_h_ = height;
        gl_present_dirty_ = true;
        ++pending_presents_;
        ui_cv_.notify_all();
    }

private:
    void UiThreadMain()
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            std::lock_guard<std::mutex> lock(ui_mutex_);
            ui_init_ok_ = false;
            ui_ready_ = true;
            ui_cv_.notify_all();
            return;
        }

        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
        if (config_.fullscreen)
        {
            flags |= SDL_WINDOW_FULLSCREEN;
        }
        if (config_.hidden)
        {
            flags |= SDL_WINDOW_HIDDEN;
        }
        if (config_.use_opengl)
        {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            flags |= SDL_WINDOW_OPENGL;
        }

        SDL_Window* win = SDL_CreateWindow(config_.title.c_str(), config_.width, config_.height, flags);
        if (win == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            std::lock_guard<std::mutex> lock(ui_mutex_);
            ui_init_ok_ = false;
            ui_ready_ = true;
            ui_cv_.notify_all();
            return;
        }

        SDL_GLContext gl_ctx = nullptr;
        GlPresentHelper gl_helper;
        sdl_internal::OpenGlPostProcess post_process;
        SDL_Renderer* ren = nullptr;
        SDL_Texture* tex = nullptr;
        int tex_w = 0;
        int tex_h = 0;

        std::filesystem::path init_shader_dir;
        std::string init_shader_id;
        {
            std::lock_guard<std::mutex> lock(g_post_shader_mutex);
            init_shader_dir = g_post_shader_dir;
            init_shader_id = g_initial_post_shader;
        }

        int gl_major = 3;
        int gl_minor = 3;
        if (config_.use_opengl)
        {
            gl_ctx = SDL_GL_CreateContext(win);
            if (gl_ctx == nullptr || !SDL_GL_MakeCurrent(win, gl_ctx) || !gl_helper.Init())
            {
                gl_helper.Shutdown();
                if (gl_ctx != nullptr)
                {
                    SDL_GL_DestroyContext(gl_ctx);
                }
                SDL_DestroyWindow(win);
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                std::lock_guard<std::mutex> lock(ui_mutex_);
                ui_init_ok_ = false;
                ui_ready_ = true;
                ui_cv_.notify_all();
                return;
            }
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &gl_major);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &gl_minor);
            const VsyncMode init_vsync = (config_.vsync_mode != VsyncMode::kOff)
                                             ? config_.vsync_mode
                                             : (config_.vsync ? VsyncMode::kOn : VsyncMode::kOff);
            g_vsync_mode.store(static_cast<int>(init_vsync));
            ApplySwapIntervalToCurrentContext(init_vsync, config_.hidden);
            g_aspect_mode.store(static_cast<int>(config_.aspect_mode));
            g_output_filter.store(static_cast<int>(config_.output_filter));
            g_fullscreen_state.store(config_.fullscreen && !config_.hidden);
            {
                std::lock_guard<std::mutex> lock(g_gl_info_mutex);
                g_gl_info.valid = true;
                g_gl_info.major = gl_major;
                g_gl_info.minor = gl_minor;
                const auto* ren_str = gl_helper.glGetString(GlPresentHelper::kGlRenderer);
                const auto* ven_str = gl_helper.glGetString(GlPresentHelper::kGlVendor);
                g_gl_info.renderer = ren_str ? reinterpret_cast<const char*>(ren_str) : "";
                g_gl_info.vendor = ven_str ? reinterpret_cast<const char*>(ven_str) : "";
            }
            std::string post_err;
            if (post_process.Initialize(init_shader_dir, false, &post_err))
            {
                g_post_process.store(&post_process);
                if (!init_shader_id.empty() && init_shader_id != kPostShaderNoneId)
                {
                    if (!post_process.Select(init_shader_id))
                    {
                        std::fprintf(stderr, "[display] post shader '%s' not applied: %s\n",
                                     init_shader_id.c_str(), post_process.last_error().c_str());
                    }
                }
            }
            else if (!init_shader_id.empty() && init_shader_id != kPostShaderNoneId)
            {
                std::fprintf(stderr, "[display] post shader '%s' not applied: %s\n",
                             init_shader_id.c_str(), post_err.c_str());
            }
            has_gl_context_.store(true);
            if (!config_.hidden)
            {
                g_overlay_window = win;
                g_overlay_gl_ctx = gl_ctx;
            }
        }
        else
        {
            g_post_process.store(nullptr);
            if (!init_shader_id.empty() && init_shader_id != kPostShaderNoneId)
            {
                std::fprintf(stderr,
                             "[display] post shader '%s' not applied: software renderer has no OpenGL context\n",
                             init_shader_id.c_str());
            }
            g_aspect_mode.store(static_cast<int>(config_.aspect_mode));
            g_output_filter.store(static_cast<int>(config_.output_filter));
            g_fullscreen_state.store(config_.fullscreen && !config_.hidden);
            {
                std::lock_guard<std::mutex> lock(g_gl_info_mutex);
                g_gl_info = DisplayGlInfo{};
            }
            ren = SDL_CreateRenderer(win, nullptr);
        }

        if (!config_.hidden)
        {
            g_title_dirty.store(false);
            SDL_SetWindowTitle(win, FormatWindowTitle(config_.title, config_.use_opengl, gl_major, gl_minor, 0.0).c_str());
        }

        bool gamepad_init = SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
        SDL_Gamepad* gamepad = gamepad_init ? OpenFirstGamepad() : nullptr;

        {
            std::lock_guard<std::mutex> lock(ui_mutex_);
            ui_init_ok_ = true;
            ui_ready_ = true;
            ui_cv_.notify_all();
        }

        std::vector<uint8_t> local_buf;
        std::deque<std::function<void()>> local_jobs;
        uint32_t last_gl_tex = 0;
        int last_gl_w = kPspBaseWidth;
        int last_gl_h = kPspBaseHeight;
        std::chrono::steady_clock::time_point fps_period_start = std::chrono::steady_clock::now();
        uint32_t fps_presents = 0;
        double last_fps = 0.0;
        g_display_fps.store(0.0);
        while (!ui_stop_.load())
        {
            {
                int req_scale = 0;
                int req_fs = -1;
                int req_vsync = -1;
                {
                    std::lock_guard<std::mutex> lock(g_display_req_mutex);
                    req_scale = g_req_window_scale;
                    g_req_window_scale = 0;
                    req_fs = g_req_fullscreen;
                    g_req_fullscreen = -1;
                    req_vsync = g_req_vsync;
                    g_req_vsync = -1;
                }
                if (req_scale >= 1 && req_scale <= 6 && !config_.hidden)
                {
                    ApplyWindowScale(win, req_scale);
                    g_fullscreen_state.store(false);
                }
                if (req_fs != -1 && !config_.hidden)
                {
                    const bool target_fs = (req_fs != 0);
                    if (((SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0) != target_fs)
                    {
                        ToggleFullscreen(win);
                    }
                }
                if (req_vsync != -1 && gl_ctx != nullptr)
                {
                    ApplySwapIntervalToCurrentContext(static_cast<VsyncMode>(req_vsync), config_.hidden);
                }
            }

            if (!config_.hidden && g_title_dirty.exchange(false))
            {
                SDL_SetWindowTitle(win, FormatWindowTitle(config_.title, config_.use_opengl, gl_major, gl_minor, last_fps).c_str());
            }

            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                {
                    close_requested_.store(true);
                }
                else if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                {
                    if (gamepad == nullptr)
                    {
                        gamepad = SDL_OpenGamepad(event.gdevice.which);
                    }
                }
                else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
                {
                    if (gamepad != nullptr && SDL_GetGamepadID(gamepad) == event.gdevice.which)
                    {
                        SDL_CloseGamepad(gamepad);
                        gamepad = OpenFirstGamepad();
                    }
                }
                if (sdl_internal::OverlayProcessEvent(event))
                {
                    continue;
                }
                if (event.type == SDL_EVENT_KEY_DOWN)
                {
                    const ShortcutResult sc = ClassifyShortcut(
                        MapSdlKeyToShortcutKey(event.key.key),
                        (event.key.mod & SDL_KMOD_ALT) != 0,
                        (event.key.mod & SDL_KMOD_CTRL) != 0,
                        event.key.repeat);
                    if (sc.kind == ShortcutKind::kWindowScale)
                    {
                        ApplyWindowScale(win, sc.value);
                    }
                    else if (sc.kind == ShortcutKind::kToggleFullscreen)
                    {
                        ToggleFullscreen(win);
                    }
                    else if (sc.kind == ShortcutKind::kRenderScale)
                    {
                        std::function<void(int)> cb;
                        {
                            std::lock_guard<std::mutex> lock(g_render_scale_mutex);
                            cb = g_render_scale_cb;
                        }
                        if (cb)
                        {
                            cb(sc.value);
                        }
                    }
                }
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                         event.button.button == SDL_BUTTON_LEFT &&
                         event.button.clicks == 2 &&
                         !sdl_internal::OverlayCapturesMouse())
                {
                    ToggleFullscreen(win);
                }
            }
            sdl_internal::PollGamepadShortcuts();
            UpdateInputSnapshotFromSdl(gamepad);

            {
                std::unique_lock<std::mutex> lock(ui_mutex_);
                if (job_queue_.empty() && !frame_dirty_ && !gl_present_dirty_ && !ui_stop_.load())
                {
                    ui_cv_.wait_for(lock, std::chrono::milliseconds(8));
                }
                if (!job_queue_.empty())
                {
                    local_jobs.swap(job_queue_);
                    job_running_ = true;
                }
            }

            if (!local_jobs.empty())
            {
                while (!local_jobs.empty())
                {
                    auto job = std::move(local_jobs.front());
                    local_jobs.pop_front();
                    if (job)
                    {
                        job();
                    }
                }
                std::lock_guard<std::mutex> lock(ui_mutex_);
                job_running_ = false;
                ui_cv_.notify_all();
            }

            int w = 0;
            int h = 0;
            bool has_frame = false;
            uint32_t present_tex = 0;
            int present_w = kPspBaseWidth;
            int present_h = kPspBaseHeight;
            bool has_gl_present = false;
            {
                std::lock_guard<std::mutex> lock(ui_mutex_);
                if (gl_present_dirty_)
                {
                    present_tex = gl_present_tex_;
                    present_w = gl_present_w_;
                    present_h = gl_present_h_;
                    gl_present_dirty_ = false;
                    has_gl_present = true;
                }
                else if (frame_dirty_)
                {
                    local_buf = frame_buf_;
                    w = frame_w_;
                    h = frame_h_;
                    frame_dirty_ = false;
                    has_frame = true;
                }
            }

            if (has_gl_present || has_frame)
            {
                const auto now = std::chrono::steady_clock::now();
                if (fps_presents == 0)
                {
                    fps_period_start = now;
                }
                ++fps_presents;
                const double seconds = std::chrono::duration<double>(now - fps_period_start).count();
                if (seconds >= 1.0)
                {
                    last_fps = static_cast<double>(fps_presents - 1) / seconds;
                    g_display_fps.store(last_fps);
                    fps_period_start = now;
                    fps_presents = 1;
                    if (!config_.hidden)
                    {
                        g_title_dirty.store(false);
                        SDL_SetWindowTitle(win, FormatWindowTitle(config_.title, config_.use_opengl, gl_major, gl_minor, last_fps).c_str());
                    }
                }
            }

            const auto aspect = static_cast<AspectMode>(g_aspect_mode.load());
            const auto out_filter = static_cast<OutputFilter>(g_output_filter.load());

            if (gl_ctx != nullptr)
            {
                int win_w = config_.width;
                int win_h = config_.height;
                SDL_GetWindowSizeInPixels(win, &win_w, &win_h);
                if (win_w <= 0) win_w = config_.width;
                if (win_h <= 0) win_h = config_.height;

                if (has_gl_present && present_tex != 0)
                {
                    last_gl_tex = present_tex;
                    last_gl_w = present_w;
                    last_gl_h = present_h;
                    gl_helper.DrawTexture(present_tex, present_w, present_h, win_w, win_h, false, aspect, out_filter,
                                          &post_process);
                    if (!config_.hidden)
                    {
                        RenderOverlay();
                        SDL_GL_SwapWindow(win);
                    }
                    std::lock_guard<std::mutex> lock(ui_mutex_);
                    if (pending_presents_ > 0)
                    {
                        --pending_presents_;
                    }
                    ui_cv_.notify_all();
                }
                else if (has_frame && w > 0 && h > 0)
                {
                    gl_helper.DrawCpuRgba8(local_buf.data(), w, h, win_w, win_h, aspect, out_filter, &post_process);
                    if (!config_.hidden)
                    {
                        RenderOverlay();
                        SDL_GL_SwapWindow(win);
                    }
                }
                else if (!config_.hidden && OverlayVisible() && last_gl_tex != 0)
                {
                    gl_helper.DrawTexture(last_gl_tex, last_gl_w, last_gl_h, win_w, win_h, false, aspect, out_filter,
                                          &post_process);
                    RenderOverlay();
                    SDL_GL_SwapWindow(win);
                }
            }
            else if (has_frame && ren != nullptr && w > 0 && h > 0)
            {
                if (tex == nullptr || tex_w != w || tex_h != h)
                {
                    if (tex != nullptr)
                    {
                        SDL_DestroyTexture(tex);
                    }
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
                    tex_w = w;
                    tex_h = h;
                }
                if (tex != nullptr)
                {
                    SDL_SetTextureScaleMode(
                        tex,
                        out_filter == OutputFilter::kLinear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
                    SDL_UpdateTexture(tex, nullptr, local_buf.data(), w * 4);
                    int ren_w = 0;
                    int ren_h = 0;
                    SDL_GetCurrentRenderOutputSize(ren, &ren_w, &ren_h);
                    const OutputRect rect = ComputeOutputRect(ren_w, ren_h, w, h, aspect);
                    const SDL_FRect dst = {
                        static_cast<float>(rect.x),
                        static_cast<float>(rect.y),
                        static_cast<float>(rect.width),
                        static_cast<float>(rect.height),
                    };
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
                    SDL_RenderClear(ren);
                    SDL_RenderTexture(ren, tex, nullptr, &dst);
                    SDL_RenderPresent(ren);
                }
            }
        }

        if (gamepad != nullptr)
        {
            SDL_CloseGamepad(gamepad);
        }
        if (gamepad_init)
        {
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
        }
        ResetInputSnapshot(true);

        if (gl_ctx != nullptr)
        {
            g_post_process.store(nullptr);
            post_process.Shutdown();
            sdl_internal::ShutdownOverlay();
            g_overlay_window = nullptr;
            g_overlay_gl_ctx = nullptr;
            gl_helper.Shutdown();
            SDL_GL_MakeCurrent(win, nullptr);
            SDL_GL_DestroyContext(gl_ctx);
        }
        if (tex != nullptr) SDL_DestroyTexture(tex);
        if (ren != nullptr) SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    DisplayConfig config_;
    bool initialized_ = false;
    SDL_Window* window_ = nullptr;
    std::atomic<bool> close_requested_{false};
    std::atomic<bool> ui_stop_{false};
    std::atomic<bool> has_gl_context_{false};
    std::thread ui_thread_;
    std::mutex ui_mutex_;
    std::condition_variable ui_cv_;
    bool ui_ready_ = false;
    bool ui_init_ok_ = false;
    std::deque<std::function<void()>> job_queue_;
    bool job_running_ = false;
    std::vector<uint8_t> frame_buf_;
    int frame_w_ = 0;
    int frame_h_ = 0;
    bool frame_dirty_ = false;
    uint32_t gl_present_tex_ = 0;
    int gl_present_w_ = 0;
    int gl_present_h_ = 0;
    bool gl_present_dirty_ = false;
    int pending_presents_ = 0;
};

class SdlInputBackend final : public InputBackend
{
public:
    bool Initialize() override
    {
        // SDL gamepad/event polling runs exclusively on SdlDisplayBackend's UI thread.
        return true;
    }

    void Shutdown() override
    {
    }

    void Poll(ControllerState& state) override
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        state = g_input_dummy ? ControllerState{} : g_input_snapshot;
    }
};

class SdlAudioBackend final : public AudioBackend
{
public:
    SdlAudioBackend() = default;
    ~SdlAudioBackend() override
    {
        Shutdown();
    }

    bool Initialize(const AudioConfig& config) override
    {
        Shutdown();

        config_ = config;
        if (config_.sample_rate <= 0)
        {
            config_.sample_rate = 44100;
        }
        if (config_.channels <= 0)
        {
            config_.channels = 2;
        }
        if (config_.buffer_frames <= 0)
        {
            config_.buffer_frames = 256;
        }

        OpenAudioDumpIfRequested();

        if (config_.dummy)
        {
            dummy_stop_ = false;
            dummy_reset_clock_ = true;
            dummy_thread_ = std::thread(&SdlAudioBackend::DummyThreadMain, this);
            initialized_ = true;
            return true;
        }

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            return false;
        }
        audio_subsys_init_ = true;

        SDL_AudioSpec spec;
        SDL_zero(spec);
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = config_.channels;
        spec.freq = config_.sample_rate;

        stream_ = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec,
            &SdlAudioBackend::SdlStreamGetCallback,
            this);
        if (stream_ == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_subsys_init_ = false;
            return false;
        }

        SDL_ResumeAudioStreamDevice(stream_);
        initialized_ = true;
        return true;
    }

    void Shutdown() override
    {
        if (stream_ != nullptr)
        {
            // Destroy stream first so SDL stops invoking SdlStreamGetCallback before callback state clears.
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(pull_mutex_);
            dummy_stop_ = true;
            pull_callback_ = nullptr;
        }
        dummy_cv_.notify_all();

        if (dummy_thread_.joinable())
        {
            dummy_thread_.join();
        }

        if (audio_subsys_init_)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_subsys_init_ = false;
        }
        CloseAudioDump();
        initialized_ = false;
    }

    void SetPullCallback(AudioPullCallback callback) override
    {
        {
            std::lock_guard<std::mutex> lock(pull_mutex_);
            pull_callback_ = std::move(callback);
            dummy_reset_clock_ = true;
        }
        dummy_cv_.notify_all();
    }

    bool QueueAudio(const int16_t* samples, size_t frame_count) override
    {
        WriteAudioDump(samples, frame_count);
        if (stream_ == nullptr)
        {
            return config_.dummy;
        }
        int byte_count = static_cast<int>(frame_count * static_cast<size_t>(config_.channels) * sizeof(int16_t));
        return SDL_PutAudioStreamData(stream_, samples, byte_count);
    }

private:
    static void SDLCALL SdlStreamGetCallback(
        void* userdata,
        SDL_AudioStream* stream,
        int additional_amount,
        int /*total_amount*/)
    {
        auto* self = static_cast<SdlAudioBackend*>(userdata);
        if (self == nullptr || stream == nullptr || additional_amount <= 0)
        {
            return;
        }

        AudioPullCallback cb;
        {
            std::lock_guard<std::mutex> lock(self->pull_mutex_);
            cb = self->pull_callback_;
        }
        if (!cb)
        {
            // Allow legacy QueueAudio push mode without zero injection.
            return;
        }

        const size_t channels = static_cast<size_t>(std::max<int32_t>(1, self->config_.channels));
        const size_t frame_bytes = channels * sizeof(int16_t);
        const size_t frames_needed = (static_cast<size_t>(additional_amount) + frame_bytes - 1) / frame_bytes;
        if (frames_needed == 0)
        {
            return;
        }

        self->sdl_scratch_.assign(frames_needed * channels, 0);
        cb(self->sdl_scratch_.data(), frames_needed);
        self->WriteAudioDump(self->sdl_scratch_.data(), frames_needed);
        SDL_PutAudioStreamData(
            stream,
            self->sdl_scratch_.data(),
            static_cast<int>(frames_needed * frame_bytes));
    }

    void DummyThreadMain()
    {
        const uint64_t rate = static_cast<uint64_t>(std::max<int32_t>(1, config_.sample_rate));
        const size_t channels = static_cast<size_t>(std::max<int32_t>(1, config_.channels));
        const size_t period_frames = static_cast<size_t>(std::max<int32_t>(64, config_.buffer_frames));
        std::vector<int16_t> scratch(period_frames * channels, 0);

        auto base_time = std::chrono::steady_clock::now();
        uint64_t pulled_frames = 0;

        for (;;)
        {
            AudioPullCallback cb;
            {
                std::unique_lock<std::mutex> lock(pull_mutex_);
                dummy_cv_.wait(lock, [this]()
                {
                    return dummy_stop_ || static_cast<bool>(pull_callback_);
                });
                if (dummy_stop_)
                {
                    break;
                }
                if (dummy_reset_clock_)
                {
                    base_time = std::chrono::steady_clock::now();
                    pulled_frames = 0;
                    dummy_reset_clock_ = false;
                }
                cb = pull_callback_;
            }

            if (!cb)
            {
                continue;
            }

            const uint64_t next_target = pulled_frames + period_frames;
            const auto next_wakeup =
                base_time + std::chrono::nanoseconds((next_target * 1000000000ull) / rate);

            {
                std::unique_lock<std::mutex> lock(pull_mutex_);
                dummy_cv_.wait_until(lock, next_wakeup, [this]()
                {
                    return dummy_stop_ || !pull_callback_ || dummy_reset_clock_;
                });
                if (dummy_stop_)
                {
                    break;
                }
                if (!pull_callback_ || dummy_reset_clock_)
                {
                    continue;
                }
                cb = pull_callback_;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now <= base_time)
            {
                continue;
            }

            const uint64_t elapsed_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - base_time).count());
            const uint64_t due_total = (elapsed_ns * rate) / 1000000000ull;
            if (due_total <= pulled_frames)
            {
                continue;
            }

            uint64_t frames_due = due_total - pulled_frames;
            const uint64_t max_burst = rate / 5ull; // cap burst to 200 ms on OS stall
            if (frames_due > max_burst)
            {
                pulled_frames = due_total - max_burst;
                frames_due = max_burst;
            }

            while (frames_due >= period_frames)
            {
                {
                    std::lock_guard<std::mutex> lock(pull_mutex_);
                    if (dummy_stop_ || !pull_callback_ || dummy_reset_clock_)
                    {
                        break;
                    }
                    cb = pull_callback_;
                }
                std::fill(scratch.begin(), scratch.end(), int16_t{0});
                cb(scratch.data(), period_frames);
                WriteAudioDump(scratch.data(), period_frames);
                pulled_frames += period_frames;
                frames_due -= period_frames;
            }
        }
    }

    static void WriteLe32(uint8_t* dst, uint32_t v)
    {
        dst[0] = static_cast<uint8_t>(v & 0xFFu);
        dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    }

    static void WriteLe16(uint8_t* dst, uint16_t v)
    {
        dst[0] = static_cast<uint8_t>(v & 0xFFu);
        dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    }

    void OpenAudioDumpIfRequested()
    {
        const char* path = std::getenv("REPSP_AUDIO_DUMP");
        if (path == nullptr || *path == '\0')
        {
            return;
        }
        std::lock_guard<std::mutex> lock(dump_mutex_);
        dump_file_.open(path, std::ios::binary | std::ios::trunc);
        if (!dump_file_.is_open())
        {
            return;
        }
        uint8_t header[44]{};
        std::memcpy(header + 0, "RIFF", 4);
        WriteLe32(header + 4, 36);
        std::memcpy(header + 8, "WAVE", 4);
        std::memcpy(header + 12, "fmt ", 4);
        WriteLe32(header + 16, 16);
        WriteLe16(header + 20, 1); // PCM
        const uint16_t channels = static_cast<uint16_t>(config_.channels);
        const uint32_t sample_rate = static_cast<uint32_t>(config_.sample_rate);
        WriteLe16(header + 22, channels);
        WriteLe32(header + 24, sample_rate);
        WriteLe32(header + 28, sample_rate * channels * 2u);
        WriteLe16(header + 32, static_cast<uint16_t>(channels * 2u));
        WriteLe16(header + 34, 16);
        std::memcpy(header + 36, "data", 4);
        WriteLe32(header + 40, 0);
        dump_file_.write(reinterpret_cast<const char*>(header), sizeof(header));
        dump_bytes_written_ = 0;
        dump_enabled_ = true;
    }

    void WriteAudioDump(const int16_t* samples, size_t frame_count)
    {
        if (!dump_enabled_ || samples == nullptr || frame_count == 0)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(dump_mutex_);
        if (!dump_file_.is_open())
        {
            return;
        }
        const size_t bytes = frame_count * static_cast<size_t>(config_.channels) * sizeof(int16_t);
        dump_file_.write(reinterpret_cast<const char*>(samples), static_cast<std::streamsize>(bytes));
        dump_bytes_written_ += static_cast<uint32_t>(bytes);
    }

    void CloseAudioDump()
    {
        std::lock_guard<std::mutex> lock(dump_mutex_);
        dump_enabled_ = false;
        if (!dump_file_.is_open())
        {
            return;
        }
        uint8_t riff_size[4];
        uint8_t data_size[4];
        WriteLe32(riff_size, 36u + dump_bytes_written_);
        WriteLe32(data_size, dump_bytes_written_);
        dump_file_.seekp(4, std::ios::beg);
        dump_file_.write(reinterpret_cast<const char*>(riff_size), 4);
        dump_file_.seekp(40, std::ios::beg);
        dump_file_.write(reinterpret_cast<const char*>(data_size), 4);
        dump_file_.close();
    }

    AudioConfig config_;
    bool initialized_ = false;
    bool audio_subsys_init_ = false;
    SDL_AudioStream* stream_ = nullptr;
    std::mutex pull_mutex_;
    std::condition_variable dummy_cv_;
    AudioPullCallback pull_callback_;
    bool dummy_stop_ = false;
    bool dummy_reset_clock_ = true;
    std::thread dummy_thread_;
    std::vector<int16_t> sdl_scratch_;
    std::atomic<bool> dump_enabled_{false};
    std::mutex dump_mutex_;
    std::ofstream dump_file_;
    uint32_t dump_bytes_written_ = 0;
};

std::unique_ptr<SdlDisplayBackend> g_display;
std::unique_ptr<SdlInputBackend> g_input;
std::unique_ptr<SdlAudioBackend> g_audio;
bool g_initialized = false;

} // namespace

bool Initialize(const PlatformConfig& config)
{
    if (g_initialized)
    {
        return true;
    }

    if (config.headless)
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }

    if (!SDL_Init(SDL_INIT_EVENTS))
    {
        return false;
    }

    ResetInputSnapshot(config.headless || config.display.dummy);

    g_display = std::make_unique<SdlDisplayBackend>();
    if (!g_display->Initialize(config.display))
    {
        // Allowed to fail in headless/dummy if window creation fails
        if (!config.display.dummy && !config.headless)
        {
            Shutdown();
            return false;
        }
    }

    g_input = std::make_unique<SdlInputBackend>();
    g_input->Initialize();

    g_audio = std::make_unique<SdlAudioBackend>();
    if (!g_audio->Initialize(config.audio))
    {
        if (!config.audio.dummy && !config.headless)
        {
            Shutdown();
            return false;
        }
    }

    g_initialized = true;
    return true;
}

void Shutdown()
{
    if (g_audio)
    {
        g_audio->Shutdown();
        g_audio.reset();
    }
    if (g_input)
    {
        g_input->Shutdown();
        g_input.reset();
    }
    if (g_display)
    {
        g_display->Shutdown();
        g_display.reset();
    }

    SDL_Quit();
    ResetInputSnapshot(true);
    g_initialized = false;
}

DisplayBackend* GetDisplay()
{
    return g_display.get();
}

InputBackend* GetInput()
{
    return g_input.get();
}

AudioBackend* GetAudio()
{
    return g_audio.get();
}

void SetDisplayTitle(const std::string& title)
{
    {
        std::lock_guard<std::mutex> lock(g_title_mutex);
        g_app_title = title;
    }
    g_title_dirty.store(true);
}

void SetRenderScaleCallback(std::function<void(int)> callback)
{
    std::lock_guard<std::mutex> lock(g_render_scale_mutex);
    g_render_scale_cb = std::move(callback);
}

void SetWindowScale(int scale)
{
    std::lock_guard<std::mutex> lock(g_display_req_mutex);
    g_req_window_scale = std::clamp(scale, 1, 6);
}

void SetFullscreen(bool fullscreen)
{
    std::lock_guard<std::mutex> lock(g_display_req_mutex);
    g_req_fullscreen = fullscreen ? 1 : 0;
}

bool IsFullscreen()
{
    return g_fullscreen_state.load();
}

void SetVsyncMode(VsyncMode mode)
{
    g_vsync_mode.store(static_cast<int>(mode));
    std::lock_guard<std::mutex> lock(g_display_req_mutex);
    g_req_vsync = static_cast<int>(mode);
}

int GetSwapInterval()
{
    return g_swap_interval.load();
}

void SetAspectMode(AspectMode mode)
{
    g_aspect_mode.store(static_cast<int>(mode));
}

AspectMode GetAspectMode()
{
    return static_cast<AspectMode>(g_aspect_mode.load());
}

void SetOutputFilter(OutputFilter filter)
{
    g_output_filter.store(static_cast<int>(filter));
}

OutputFilter GetOutputFilter()
{
    return static_cast<OutputFilter>(g_output_filter.load());
}

double GetDisplayFps()
{
    return g_display_fps.load();
}

DisplayGlInfo GetDisplayGlInfo()
{
    std::lock_guard<std::mutex> lock(g_gl_info_mutex);
    return g_gl_info;
}

uint64_t GetProcessMemoryBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return static_cast<uint64_t>(pmc.WorkingSetSize);
    }
    return 0;
#else
    std::ifstream in("/proc/self/statm");
    uint64_t size_pages = 0;
    uint64_t resident_pages = 0;
    if (in >> size_pages >> resident_pages)
    {
        const long page_size = sysconf(_SC_PAGESIZE);
        if (page_size > 0)
        {
            return resident_pages * static_cast<uint64_t>(page_size);
        }
    }
    return 0;
#endif
}

void SetPostShaderDirectory(const std::filesystem::path& directory)
{
    std::lock_guard<std::mutex> lock(g_post_shader_mutex);
    g_post_shader_dir = directory;
}

std::filesystem::path GetPostShaderDirectory()
{
    std::lock_guard<std::mutex> lock(g_post_shader_mutex);
    return g_post_shader_dir;
}

void SetInitialPostShader(std::string id)
{
    std::lock_guard<std::mutex> lock(g_post_shader_mutex);
    g_initial_post_shader = id.empty() ? std::string(kPostShaderNoneId) : std::move(id);
}

PostShaderControl* GetPostShaderControl()
{
    return g_post_process.load();
}

namespace sdl_internal
{

SDL_Window* OverlayWindow()
{
    return g_overlay_window;
}

SDL_GLContext OverlayContext()
{
    return g_overlay_gl_ctx;
}

bool DisplayIsEs()
{
    return false;
}

} // namespace sdl_internal

} // namespace repsp::platform
