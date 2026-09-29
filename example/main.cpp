// -----------------------------------------------------------------------------
// main.cpp — application entry: window, OpenGL context, and the render loop
// -----------------------------------------------------------------------------
// Order matters in OpenGL-heavy apps: GLAD must be included before anything
// that pulls in OpenGL headers, so the loader can typedef all GL symbols.
// GLM_ENABLE_EXPERIMENTAL enables extra headers (e.g. gtx) used elsewhere.
#define GLM_ENABLE_EXPERIMENTAL

#include <glad/glad.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>


#include <vector>
#include <string>
#include <array>
#include <iostream>
#include <ctime>
#include <algorithm>
#include <cstdlib>

#include "util/Timer.h"
#include <cmath>

#include "util/DebugLog.h"

#include "render/animation.h"
#include "render/Mesh.h"
#include "render/point.h"
#include "render/Light.h"
#include "GameState.h"
#include "core/Window.h"
#include "util/DebugHooks.h"

// When true, mouse motion rotates the camera; SDL "relative" mode hides the cursor
// and delivers delta movement instead of absolute position.
bool moveCamera = false;

// Global-ish game state: FPS counter, future scene objects, etc.
GameState gs = GameState();

int main(int argc, char* argv[])
{
	// Boot SDL, create an OpenGL 3.3 core context, load GLAD, set GL defaults.
	SDLState state = initialize(1600, 900, { .title = "OpenGL Quickstart" });

	if (!state.successfullyInitialized)
	{
		return 1;
	}

	// World-units per second for keyboard camera translation (scaled by deltaTime).
	float cameraSpeed = 20.0f;

	// Color used when clearing the color buffer (RGBA in 0..1). Alpha is often 1.
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

	// Build shader programs from disk. QS_SHADER_DIR points at the package's stock shaders; your own go relative to the working dir.
	// Index 0: textured mesh pipeline; index 1: simple point pipeline.
	std::vector<Shader> shaders = { Shader(QS_SHADER_DIR "default.vert", QS_SHADER_DIR "default.frag"), Shader(QS_SHADER_DIR "point.vert", QS_SHADER_DIR "point.frag") };

	Resources res(shaders);

	// Fill meshes, lights, UBOs, etc. — see Resources::load() (currently mostly examples in comments).
	res.load();

	// debug: QS_CAMERA="x,y,z[,yaw,pitch]" places the camera at startup
	applyCameraOverride(res.camera, std::getenv("QS_CAMERA"));

	// debug: QS_SHOT=<path> renders for QS_SHOT_AFTER seconds (default 2), writes
	// one PNG and exits, so "tweak, look at the result" can be scripted
	const char* shotPath = std::getenv("QS_SHOT");
	float shotAfter = 2.0f;
	if (const char* a = std::getenv("QS_SHOT_AFTER")) shotAfter = static_cast<float>(atof(a));
	float shotElapsed = 0.0f;

	int fpsBuffer = 0;

	bool running = true;

	// Frame timing: SDL_GetTicks() is milliseconds since SDL_Init; deltaTime is seconds per frame.
	uint64_t prevTime = SDL_GetTicks();

	// Cache uniform locations once; avoids repeated lookups in the hot loop.
	const GLint numScenarioLightsLoc = glGetUniformLocation(res.shaderProgram[0].ID, "numScenarioLights");
	const GLint texCoordOffsetLoc = glGetUniformLocation(res.shaderProgram[0].ID, "texCoordOffset");

	while (running)
	{
		DBG_IF(flux::verbose, "[main loop] New Frame Computation");
		uint64_t nowTime = SDL_GetTicks();
		float deltaTime = (nowTime - prevTime) / 1000.0f;

		SDL_Event event{ 0 };

		// Drain all pending events this frame so input feels responsive.
		while (SDL_PollEvent(&event))
		{

			if (event.type == SDL_EVENT_MOUSE_MOTION && moveCamera) {
				// Mouse deltas update yaw (around Y) and pitch (up/down). Clamp pitch to avoid flip.
				res.camera.yaw += event.motion.xrel * res.camera.sensitivity;
				res.camera.pitch -= event.motion.yrel * res.camera.sensitivity;

				if (res.camera.pitch > 84.0f) res.camera.pitch = 84.0f;
				if (res.camera.pitch < -84.0f) res.camera.pitch = -84.0f;
				// Optional yaw limits for a constrained view:
				//if (res.camera.yaw > -44.0f) res.camera.yaw = -44.0f;
				//if (res.camera.yaw < -134.0f) res.camera.yaw = -134.0f;

				// Classic FPS-style direction from yaw/pitch (Y-up, forward into -Z by default setup).
				glm::vec3 direction;
				direction.x = cos(glm::radians(res.camera.yaw)) * cos(glm::radians(res.camera.pitch));
				direction.y = sin(glm::radians(res.camera.pitch));
				direction.z = sin(glm::radians(res.camera.yaw)) * cos(glm::radians(res.camera.pitch));
				res.camera.orientation = glm::normalize(direction);

			}

			switch (event.type)
			{
			case SDL_EVENT_QUIT:
			{
				running = false;
				break;
			}
			case SDL_EVENT_WINDOW_RESIZED: {
				// Use drawable pixel size (not logical window size) for HiDPI-correct viewport/projection.
				int drawableW = 0;
				int drawableH = 0;
				SDL_GetWindowSizeInPixels(state.window, &drawableW, &drawableH);
				state.width = std::max(drawableW, 10);
				state.height = std::max(drawableH, 10);
				glViewport(0, 0, state.width, state.height);
				break;
			}
			case SDL_EVENT_KEY_DOWN:

				// debug: dump the current frame to a PNG
				if (event.key.scancode == SDL_SCANCODE_F12)
				{
					saveScreenshot(state, "screenshot.png");
				}
				if (event.key.scancode == SDL_SCANCODE_O)
				{
					// Toggle look mode: relative mouse for camera vs normal OS cursor.
					moveCamera = !moveCamera;
					SDL_SetWindowRelativeMouseMode(state.window, moveCamera);
				}
				if (event.key.scancode == SDL_SCANCODE_P)
				{
					// Reset camera to a known forward (-Z) and level pitch.
					res.camera.yaw = -90.0f;
					res.camera.pitch = 0.0f;
					res.camera.orientation = glm::vec3(0.0f, 0.0f, -1.0f);
				}
				if (event.key.scancode == SDL_SCANCODE_E)
				{
					res.camera.position -= glm::vec3(0.0f, 1.00f, 0.0f) * cameraSpeed * deltaTime;
					res.scenarioLights[1].position = glm::vec4(res.camera.position, 0.0f);
				}
				if (event.key.scancode == SDL_SCANCODE_Q)
				{
					res.camera.position += glm::vec3(0.0f, 1.00f, 0.0f) * cameraSpeed * deltaTime;
					res.scenarioLights[1].position = glm::vec4(res.camera.position, 0.0f);
				}
				if (event.key.scancode == SDL_SCANCODE_W)
				{
					res.camera.position -= glm::vec3(0.0f, 0.0f, 1.00f) * cameraSpeed * deltaTime;
					res.scenarioLights[1].position = glm::vec4(res.camera.position, 0.0f);
				}
				if (event.key.scancode == SDL_SCANCODE_S)
				{
					res.camera.position += glm::vec3(0.0f, 0.0f, 1.00f) * cameraSpeed * deltaTime;
					res.scenarioLights[1].position = glm::vec4(res.camera.position, 0.0f);
				}
				if (event.key.scancode == SDL_SCANCODE_D)
				{
					res.camera.position += glm::vec3(1.00f, 0.0f, 0.0f) * cameraSpeed * deltaTime;
					res.scenarioLights[1].position = glm::vec4(res.camera.position, 0.0f);
				}
				if (event.key.scancode == SDL_SCANCODE_A)
				{
					res.camera.position -= glm::vec3(1.00f, 0.0f, 0.0f) * cameraSpeed * deltaTime;
					res.scenarioLights[1].position = glm::vec4(res.camera.position, 0.0f);
				}
				break;
			case SDL_EVENT_KEY_UP:
				break;
			default:
				break;
			}
		}

		// FPS estimate: count frames per Timer interval, then scale (see gs.fps * 10 below).
		gs.FPSTimer.step(deltaTime);
		fpsBuffer += 1;

		if (gs.FPSTimer.isTimedOut())
		{
			gs.FPSTimer.reset();
			gs.fps = fpsBuffer;
			fpsBuffer = 0;
		}

		DBG_IF(flux::verbose, "FPS", gs.fps * 10);

		// Reset color and depth buffers before drawing the new frame.
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		res.shaderProgram[0].Activate();

		// Recompute projection * view and upload as one matrix (see Camera::updateMatrix).
		res.camera.updateMatrix(90.0f, 0.1f, 100.0f, state.width, state.height);
		

		// Stream light array to the uniform buffer bound to binding point 0 in the shader.
		glBindBuffer(GL_UNIFORM_BUFFER, res.UBO);

		glBufferSubData(
			GL_UNIFORM_BUFFER,
			0,
			res.scenarioLights.size() * sizeof(Light),
			res.scenarioLights.data()
		);

		glUniform1i(numScenarioLightsLoc, static_cast<int>(res.scenarioLights.size()));

		GameObject obj = gs.scenarioObjects[0];

		// Atlas UV offset for current animation frame (shader adds this to mask UVs).
		glUniform2f(texCoordOffsetLoc, obj.anims[0].getSpriteUVOffsetX(), obj.anims[0].getSpriteUVOffsetY());
		
		gs.scenarioObjects[0].Draw(res, res.shaderProgram[0], res.camera);
		
		// res.shaderProgram[1].Activate();
		// res.points[0].Draw(res.shaderProgram[1], res.camera);

		if (shotPath)
		{
			shotElapsed += deltaTime;
			if (shotElapsed >= shotAfter)
			{
				saveScreenshot(state, shotPath);
				running = false;
			}
		}

		// Present the back buffer: double-buffering means you draw off-screen, then swap.
		SDL_GL_SwapWindow(state.window);

		uint64_t final_time = SDL_GetTicks();

		MDBG_IF(flux::verbose, DBG_N("delta", final_time - nowTime), DBG_N("frame-time", 1 / (double) gs.fpsLimit));
		while ((final_time - nowTime)/ 1000.0f < 1 / (double) gs.fpsLimit)
		{
			final_time = SDL_GetTicks();
		}

		prevTime = nowTime;
	}

	for (Mesh& mesh : res.meshs)
	{
		mesh.Delete();
	}
	res.shaderProgram[0].Delete();
	res.shaderProgram[1].Delete();
	// the GL deletes above need the context, so it goes last
	cleanup(state);

	return 0;
}
