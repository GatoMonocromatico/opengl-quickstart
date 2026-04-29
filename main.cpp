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

#include "Timer.h"
#include <cmath>

#include "animation.h"
#include "Mesh.h"
#include "point.h"
#include "Light.h"
#include "GameState.h"

// When true, mouse motion rotates the camera; SDL "relative" mode hides the cursor
// and delivers delta movement instead of absolute position.
bool moveCamera = false;

SDLState initialize(int width, int height);
void cleanup(SDLState& state);

static void GLAPIENTRY glDebugOutput(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	const void* userParam
)
{
	(void)source;
	(void)type;
	(void)id;
	(void)severity;
	(void)length;
	(void)userParam;
	std::cerr << "[OpenGL] " << message << std::endl;
}

// Global-ish game state: FPS counter, future scene objects, etc.
GameState gs = GameState();

int main(int argc, char* argv[])
{
	// Boot SDL, create an OpenGL 3.3 core context, load GLAD, set GL defaults.
	SDLState state = initialize(1600, 900);

	if (!state.successfullyInitialized)
	{
		return 1;
	}

	// World-units per second for keyboard camera translation (scaled by deltaTime).
	float cameraSpeed = 20.0f;

	// Color used when clearing the color buffer (RGBA in 0..1). Alpha is often 1.
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

	// Build shader programs from disk (vertex + fragment paths are relative to working dir).
	// Index 0: textured mesh pipeline; index 1: simple point pipeline.
	std::vector<Shader> shaders = { Shader("default.vert", "default.frag"), Shader("point.vert", "point.frag") };

	Resources res(shaders);

	// Fill meshes, lights, UBOs, etc. — see Resources::load() (currently mostly examples in comments).
	res.load();

	int fpsBuffer = 0;

	bool running = true;

	// Frame timing: SDL_GetTicks() is milliseconds since SDL_Init; deltaTime is seconds per frame.
	uint64_t prevTime = SDL_GetTicks();

	// Cache uniform locations once; avoids repeated lookups in the hot loop.
	const GLint numScenarioLightsLoc = glGetUniformLocation(res.shaderProgram[0].ID, "numScenarioLights");
	const GLint texCoordOffsetLoc = glGetUniformLocation(res.shaderProgram[0].ID, "texCoordOffset");

	while (running)
	{
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

		// Present the back buffer: double-buffering means you draw off-screen, then swap.
		SDL_GL_SwapWindow(state.window);

		prevTime = nowTime;
	}

	cleanup(state);
	for (Mesh& mesh : res.meshs)
	{
		mesh.Delete();
	}
	res.shaderProgram[0].Delete();
	res.shaderProgram[1].Delete();

	return 0;
}

SDLState initialize(int width, int height)
{
	SDL_Window* window = nullptr;
	SDL_GLContext context = nullptr;
	auto fail = [&](const std::string& where) {
		std::string msg = where + ": " + SDL_GetError();
		std::cerr << msg << std::endl;
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization Error", msg.c_str(), nullptr);
		if (context) {
			SDL_GL_DestroyContext(context);
			context = nullptr;
		}
		if (window) {
			SDL_DestroyWindow(window);
			window = nullptr;
		}
		SDL_Quit();
		return SDLState(width, height, width, height, nullptr, nullptr, false);
	};

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		return fail("SDL_Init(SDL_INIT_VIDEO) failed");
	}

	// Request an OpenGL 3.3 core profile context before creating the window.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	int contextFlags = 0;
#ifdef __APPLE__
	contextFlags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
#endif
#ifndef NDEBUG
	contextFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
#endif
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, contextFlags);
	// Depth buffer bit depth; required for glEnable(GL_DEPTH_TEST) to resolve occlusion correctly.
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	const SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	window = SDL_CreateWindow("Tetris Roguelike", width, height, windowFlags);
	if (!window) {
		return fail("SDL_CreateWindow failed");
	}
	SDL_SetWindowMinimumSize(window, 10, 10);

	// GL context holds all OpenGL state for this thread; one window typically has one context.
	context = SDL_GL_CreateContext(window);
	if (!context) {
		return fail("SDL_GL_CreateContext failed");
	}

	if (!SDL_GL_MakeCurrent(window, context)) {
		return fail("SDL_GL_MakeCurrent failed");
	}

	if (!SDL_GL_SetSwapInterval(1)) {
		return fail("SDL_GL_SetSwapInterval(1) failed");
	}

	// GLAD loads function pointers for this OpenGL version; SDL supplies the loader hook.
	if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
		return fail("gladLoadGLLoader failed");
	}

	// GL_BLEND: alpha transparency; GL_DEPTH_TEST: near fragments win; GL_PROGRAM_POINT_SIZE: allow gl_PointSize in shaders.
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_PROGRAM_POINT_SIZE);
	// Standard premultiplied-style blending for textures with alpha.
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	int drawableW = 0;
	int drawableH = 0;
	SDL_GetWindowSizeInPixels(window, &drawableW, &drawableH);
	drawableW = std::max(drawableW, 10);
	drawableH = std::max(drawableH, 10);
	glViewport(0, 0, drawableW, drawableH);

// #ifndef NDEBUG
// 	if (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug) {
// 		glEnable(GL_DEBUG_OUTPUT);
// 		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
// 		glDebugMessageCallback(glDebugOutput, nullptr);
// 	}
// #endif

	SDLState state(drawableW, drawableH, width, height, window, context, true);

	return state;
}

void cleanup(SDLState& state)
{
	if (state.context) {
		SDL_GL_DestroyContext(state.context);
		state.context = nullptr;
	}
	if (state.window) {
		SDL_DestroyWindow(state.window);
		state.window = nullptr;
	}
	SDL_Quit();
}
