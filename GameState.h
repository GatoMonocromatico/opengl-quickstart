#pragma once
// -----------------------------------------------------------------------------
// GameState.h — per-run simulation state plus SDL/OpenGL window bundle
// -----------------------------------------------------------------------------

#include "Timer.h"
#include "Resources.h"
#include "GameObject.h"


// Everything SDL needs to show a window and an OpenGL context on this thread.
struct SDLState
{
	SDL_Window* window;
	SDL_GLContext context;
	int width, height, logW, logH;
	const bool* keys;
	bool successfullyInitialized;

	SDLState(int w, int h, int logicalW, int logicalH, SDL_Window* win, SDL_GLContext ctxt, bool initSuccess) :
		keys(SDL_GetKeyboardState(nullptr)),
		width(w),
		height(h),
		logW(logicalW),
		logH(logicalH),
		window(win),
		context(ctxt),
		successfullyInitialized(initSuccess)
	{
	}
};

// Owns lightweight counters and the list of logical objects in your scene.
struct GameState
{
	// Fires roughly every 0.1s so we can count frames in that window for an FPS readout.
	Timer FPSTimer = Timer(0.1f);

	int fps = 0;

	// Each GameObject points at a Mesh index inside Resources and carries its own model matrix.
	std::vector<GameObject> scenarioObjects;

	GameState()
	{
		// Example: GameObject(ObjectType, dataIndex, meshIndex, animations, currentAnimIndex)
		scenarioObjects.push_back(GameObject(ObjectType::level, 0, 0));
	}
};
