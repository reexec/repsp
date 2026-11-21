// Overlay contract on SDL3 and OpenGL 3: Dear ImGui with its SDL3 and
// OpenGL3 backends.

#include "repsp/platform/overlay.h"

#include "display_sdl_internal.h"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <utility>

namespace repsp::platform
{

namespace
{

std::mutex g_builder_mutex;
std::function<void()> g_builder;
std::atomic<bool> g_visible{false};

// Touched only on the thread that owns the GL context.
bool g_initialized = false;
bool g_failed = false;

// Keep the overlay proportional to the window: base sizes target a 960x720
// window and scale with the smaller axis ratio, clamped to [1.0, 8.0]. The
// floor keeps text at the font's own size in small windows (a 480x272
// picture at 1x would otherwise shrink it to about a third); there the
// application fits its windows to the viewport and lets them scroll.
ImGuiStyle g_base_style;
float g_ui_scale = 0.0f;

void FollowWindowSize(SDL_Window* window)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    const float scale = std::clamp(
        std::min(static_cast<float>(width) / 960.0f, static_cast<float>(height) / 720.0f),
        1.0f,
        8.0f);
    if (std::fabs(scale - g_ui_scale) < 0.001f)
    {
        return;
    }
    g_ui_scale = scale;
    ImGuiStyle& style = ImGui::GetStyle();
    style = g_base_style;
    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
}

GamepadShortcutState g_shortcut_state{};

void Toggle(const char* how)
{
    const bool visible = !g_visible.load();
    g_visible.store(visible);
    std::fprintf(stderr, "[overlay] menu %s (%s)\n", visible ? "opened" : "closed", how);
}

bool Initialize(SDL_Window* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // settings live in the application's own file
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    g_base_style = ImGui::GetStyle();
    g_ui_scale = 0.0f;
    const bool es = sdl_internal::DisplayIsEs();
    if (!ImGui_ImplSDL3_InitForOpenGL(window, sdl_internal::OverlayContext()))
    {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init(es ? "#version 300 es" : "#version 330 core"))
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    return true;
}

} // namespace

void SetOverlayBuilder(std::function<void()> builder)
{
    std::lock_guard<std::mutex> lock(g_builder_mutex);
    g_builder = std::move(builder);
}

bool OverlayVisible()
{
    return g_visible.load();
}

void SetOverlayVisible(bool visible)
{
    g_visible.store(visible);
}

void RenderOverlay()
{
    std::function<void()> builder;
    {
        std::lock_guard<std::mutex> lock(g_builder_mutex);
        builder = g_builder;
    }
    SDL_Window* window = sdl_internal::OverlayWindow();
    if (!builder || window == nullptr || g_failed)
    {
        return;
    }
    if (!g_initialized)
    {
        g_initialized = Initialize(window);
        if (!g_initialized)
        {
            g_failed = true;
            std::fprintf(stderr, "[overlay] Dear ImGui could not start; no overlay\n");
            return;
        }
    }
    FollowWindowSize(window);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    builder();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

namespace sdl_internal
{

bool OverlayProcessEvent(const SDL_Event& event)
{
    if (OverlayWindow() == nullptr)
    {
        return false;
    }
    if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
        event.key.key == SDLK_TAB &&
        (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) == 0)
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            Toggle("Tab");
        }
        return true;
    }
    if (g_initialized)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
    return false;
}

bool OverlayCapturesMouse()
{
    if (!g_initialized || !g_visible.load())
    {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
}

GamepadShortcutResult PollGamepadShortcuts()
{
    bool any_triggers_down = false;
    bool any_menu_y = false;
    bool any_quit_l3r3 = false;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids != nullptr && i < count; ++i)
    {
        SDL_Gamepad* gamepad = SDL_GetGamepadFromID(ids[i]);
        if (gamepad == nullptr)
        {
            continue;
        }
        const int16_t lt = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        const int16_t rt = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (lt > kGamepadTriggerDownThreshold && rt > kGamepadTriggerDownThreshold)
        {
            any_triggers_down = true;
            any_menu_y = any_menu_y || SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
            any_quit_l3r3 = any_quit_l3r3 ||
                            (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK) &&
                             SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        }
    }
    if (ids != nullptr)
    {
        SDL_free(ids);
    }

    GamepadShortcutInput input{};
    if (any_triggers_down)
    {
        input.left_trigger = 32767;
        input.right_trigger = 32767;
        input.y = any_menu_y;
        input.l3 = any_quit_l3r3;
        input.r3 = any_quit_l3r3;
    }
    const bool allow_menu = (OverlayWindow() != nullptr);
    const GamepadShortcutResult res =
        UpdateGamepadShortcuts(g_shortcut_state, input, SDL_GetTicks(), allow_menu);
    if (res.toggle_menu)
    {
        Toggle("LT+RT+Y");
    }
    if (res.request_quit)
    {
        std::fprintf(stderr, "[overlay] quit requested (LT+RT+L3+R3)\n");
    }
    return res;
}

void ShutdownOverlay()
{
    if (!g_initialized)
    {
        return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_initialized = false;
}

} // namespace sdl_internal

} // namespace repsp::platform
