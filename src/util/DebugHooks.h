#pragma once
// -----------------------------------------------------------------------------
// DebugHooks.h — small helpers that make visual work scriptable
// -----------------------------------------------------------------------------
// Each app picks its own env var names and passes the value in, e.g.
//   applyCameraOverride(res.camera, std::getenv("QS_CAMERA"));

#include "core/Window.h"
#include "render/camera.h"

// Writes the current back buffer to a PNG. Reads the framebuffer directly
// rather than relying on a window manager screenshot, so the capture is exactly
// one rendered frame with no compositor or focus involvement.
void saveScreenshot(const SDLState& state, const char* path);

// spec = "x,y,z[,yaw,pitch]" replaces the camera's placement. Null does nothing,
// so an unset env var costs nothing.
void applyCameraOverride(Camera& camera, const char* spec);
