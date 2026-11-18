// Between the SDL display and the overlay built on it (not a platform
// contract; only src/platform/sdl/ includes this).

#ifndef REPSP_PLATFORM_SDL_DISPLAY_SDL_INTERNAL_H
#define REPSP_PLATFORM_SDL_DISPLAY_SDL_INTERNAL_H

#include <SDL3/SDL.h>

namespace repsp::platform::sdl_internal
{

// sdl_platform.cpp: the visible OpenGL window (nullptr when hidden, software,
// or dummy), its GL context, and whether that context is OpenGL ES.
SDL_Window* OverlayWindow();
SDL_GLContext OverlayContext();
bool DisplayIsEs();

// overlay_sdl.cpp: processes an SDL event for the overlay; returns true when
// it was the menu toggle (Tab) and must go no further.
bool OverlayProcessEvent(const SDL_Event& event);

// True when the overlay is open and currently captures mouse input (hovering a menu window).
bool OverlayCapturesMouse();

// Gamepad shortcuts: LB+RB+Y toggles the menu.
void PollGamepadShortcuts();

// Releases the ImGui context and backends; called on the UI thread before the
// GL context is destroyed.
void ShutdownOverlay();

} // namespace repsp::platform::sdl_internal

#endif // REPSP_PLATFORM_SDL_DISPLAY_SDL_INTERNAL_H
