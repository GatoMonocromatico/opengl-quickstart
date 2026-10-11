#pragma once
// -----------------------------------------------------------------------------
// Window.h — SDL window + OpenGL 3.3 core context bring-up and teardown
// -----------------------------------------------------------------------------
// initialize() boots SDL, asks for the dedicated GPU on hybrid laptops, creates
// the window and context, loads GLAD and sets the GL defaults (blend, depth test,
// program point size). cleanup() undoes all of it.

#include <glad/glad.h>
#include <SDL3/SDL.h>

// Everything SDL needs to show a window and an OpenGL context on this thread.
struct SDLState
{
	SDL_Window* window;
	SDL_GLContext context;
	int width, height, logW, logH;
	const bool* keys;
	bool successfullyInitialized;

	SDLState(int w, int h, int logicalW, int logicalH, SDL_Window* win, SDL_GLContext ctxt, bool initSuccess) :
		window(win),
		context(ctxt),
		width(w),
		height(h),
		logW(logicalW),
		logH(logicalH),
		keys(SDL_GetKeyboardState(nullptr)),
		successfullyInitialized(initSuccess)
	{
	}
};

struct WindowOptions
{
	const char* title = "OpenGL Quickstart";
	bool resizable = true;
	// off leaves frame pacing to the app (e.g. a busy-wait fps limit)
	bool vsync = true;
	// samples per pixel on the window's framebuffer, 0 for no multisampling
	int msaa = 0;
	// Sets SDL's app id before SDL_Init: on X11 it becomes the WM_CLASS (so a WM
	// rule like i3's `for_window [class="..."]` can match), on Wayland the app_id.
	// Null leaves SDL's default.
	const char* appId = nullptr;
	bool borderless = false;
	bool alwaysOnTop = false;
	// ARGB window: whatever is cleared with alpha 0 shows the desktop behind it.
	// Needs a compositor (picom) to look transparent, opaque black without one.
	bool transparent = false;
	// true asks hybrid-graphics laptops for the dedicated GPU (see the README);
	// false stays on the default GPU, which keeps an always-running small app from
	// waking the dedicated one
	bool preferDedicatedGpu = true;
};

// On failure everything created so far is already released and
// successfullyInitialized is false.
SDLState initialize(int width, int height, const WindowOptions& options = {});
void cleanup(SDLState& state);
