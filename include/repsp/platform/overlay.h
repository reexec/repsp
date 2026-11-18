// Platform contract: the overlay drawn over the picture (Dear ImGui), such as
// the application's settings screen.
//
// The platform owns the ImGui context and its window and GL backends; the
// application registers a builder that makes ImGui calls (it includes imgui.h
// itself). The overlay is created and drawn only with the OpenGL renderer in
// a visible window.

#ifndef REPSP_PLATFORM_OVERLAY_H
#define REPSP_PLATFORM_OVERLAY_H

#include <functional>

namespace repsp::platform
{

// The function that issues the overlay's ImGui calls. It runs on every
// overlay render pass, whether the menu is visible or not (the caller decides
// what to draw), between ImGui::NewFrame and ImGui::Render.
void SetOverlayBuilder(std::function<void()> builder);

// Draws the overlay into the window's framebuffer, which must be bound, just
// before swapping buffers, on the thread that owns the GL context. The first
// call creates the ImGui context; GL state is restored afterwards.
void RenderOverlay();

// Whether the menu is open. Tab toggles it in the window, and LB+RB+Y on a
// gamepad (L+R+Triangle in the PSP mapping).
// While it is open the game reads no input (InputBackend::Poll reports nothing
// pressed) and input goes to the overlay.
bool OverlayVisible();
void SetOverlayVisible(bool visible);

} // namespace repsp::platform

#endif // REPSP_PLATFORM_OVERLAY_H
