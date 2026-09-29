#pragma once
// -----------------------------------------------------------------------------
// GameState.h — per-run simulation state plus SDL/OpenGL window bundle
// -----------------------------------------------------------------------------

#include "util/DebugLog.h"
#include "core/Window.h"
#include "util/Timer.h"
#include "Resources.h"
#include "gameObject.h"


// Owns lightweight counters and the list of logical objects in your scene.
struct GameState
{
	// Fires roughly every 0.1s so we can count frames in that window for an FPS readout.
	Timer FPSTimer = Timer(0.1f);

	int fps = 0;
	int fpsLimit = 10;

	// Each GameObject points at a Mesh index inside Resources and carries its own model matrix.
	std::vector<GameObject> scenarioObjects;

	GameState()
	{
		// Example: GameObject(ObjectType, dataIndex, meshIndex, animations, currentAnimIndex)
		scenarioObjects.push_back(GameObject(ObjectType::level, 0, 0));
	}
};
