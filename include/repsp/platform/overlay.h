// Platform contract: the overlay drawn over the picture (Dear ImGui), such as
// the application's settings screen.
//
// The platform owns the ImGui context and its window and GL backends; the
// application registers a builder that makes ImGui calls (it includes imgui.h
// itself). The overlay is created and drawn only with the OpenGL renderer in
// a visible window.

#ifndef REPSP_PLATFORM_OVERLAY_H
#define REPSP_PLATFORM_OVERLAY_H

#include <cstdint>
#include <functional>

namespace repsp::platform
{

inline constexpr int16_t kGamepadTriggerDownThreshold = 16384;
inline constexpr uint64_t kGamepadQuitHoldMs = 1000;

struct GamepadShortcutInput
{
    int16_t left_trigger = 0;  // 0..32767; down when > kGamepadTriggerDownThreshold
    int16_t right_trigger = 0; // 0..32767; down when > kGamepadTriggerDownThreshold
    bool y = false;            // North face button
    bool l3 = false;           // Left stick click
    bool r3 = false;           // Right stick click
};

struct GamepadShortcutState
{
    bool menu_combo_held = false;
    bool quit_timing = false;
    uint64_t quit_since_ms = 0;
    bool quit_fired = false;
};

struct GamepadShortcutResult
{
    bool toggle_menu = false;
    bool request_quit = false;
    bool withhold_y = false;
    bool withhold_l3 = false;
    bool withhold_r3 = false;
};

// Pure deterministic state transition for gamepad shortcuts:
//   * LT+RT+Y toggles the settings overlay on press (when allow_menu is true).
//   * LT+RT+L3+R3 held for kGamepadQuitHoldMs requests quit once per hold.
//   * While LT+RT are both held past kGamepadTriggerDownThreshold, Y, L3, and
//     R3 are withheld from game input even when the overlay is closed.
constexpr GamepadShortcutResult UpdateGamepadShortcuts(
    GamepadShortcutState& state,
    const GamepadShortcutInput& input,
    uint64_t now_ms,
    bool allow_menu = true) noexcept
{
    GamepadShortcutResult out{};
    const bool triggers_down = (input.left_trigger > kGamepadTriggerDownThreshold) &&
                               (input.right_trigger > kGamepadTriggerDownThreshold);
    if (triggers_down)
    {
        out.withhold_y = true;
        out.withhold_l3 = true;
        out.withhold_r3 = true;
    }

    const bool menu_down = triggers_down && input.y;
    if (allow_menu && menu_down && !state.menu_combo_held)
    {
        out.toggle_menu = true;
    }
    state.menu_combo_held = menu_down;

    const bool quit_down = triggers_down && input.l3 && input.r3;
    if (!quit_down)
    {
        state.quit_timing = false;
        state.quit_since_ms = 0;
        state.quit_fired = false;
        return out;
    }

    if (!state.quit_timing)
    {
        state.quit_timing = true;
        state.quit_since_ms = now_ms;
    }
    if (!state.quit_fired && now_ms >= state.quit_since_ms &&
        (now_ms - state.quit_since_ms) >= kGamepadQuitHoldMs)
    {
        state.quit_fired = true;
        out.request_quit = true;
    }
    return out;
}

// The function that issues the overlay's ImGui calls. It runs on every
// overlay render pass, whether the menu is visible or not (the caller decides
// what to draw), between ImGui::NewFrame and ImGui::Render.
void SetOverlayBuilder(std::function<void()> builder);

// Draws the overlay into the window's framebuffer, which must be bound, just
// before swapping buffers, on the thread that owns the GL context. The first
// call creates the ImGui context; GL state is restored afterwards.
void RenderOverlay();

// Whether the menu is open. Tab toggles it in the window, and LT+RT+Y on a
// gamepad.
// While it is open the game reads no input (InputBackend::Poll reports nothing
// pressed) and input goes to the overlay.
bool OverlayVisible();
void SetOverlayVisible(bool visible);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_OVERLAY_H
