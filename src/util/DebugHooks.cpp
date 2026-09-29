#define GLM_ENABLE_EXPERIMENTAL
#include "util/DebugHooks.h"

#include <SDL3_image/SDL_image.h>
#include <vector>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cmath>

void saveScreenshot(const SDLState& state, const char* path)
{
	const int w = state.width;
	const int h = state.height;
	std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

	// GL's origin is bottom-left, an image file's is top-left.
	const size_t stride = static_cast<size_t>(w) * 4;
	std::vector<unsigned char> flipped(pixels.size());
	for (int y = 0; y < h; y++)
	{
		std::memcpy(flipped.data() + static_cast<size_t>(y) * stride,
		            pixels.data() + static_cast<size_t>(h - 1 - y) * stride,
		            stride);
	}

	SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32,
		flipped.data(), static_cast<int>(stride));
	if (!surface)
	{
		std::cerr << "screenshot: " << SDL_GetError() << std::endl;
		return;
	}
	if (!IMG_SavePNG(surface, path))
	{
		std::cerr << "screenshot: " << SDL_GetError() << std::endl;
	}
	else
	{
		std::cout << "screenshot -> " << path << std::endl;
	}
	SDL_DestroySurface(surface);
}

void applyCameraOverride(Camera& camera, const char* spec)
{
	if (!spec) return;

	float v[5] = { camera.position.x, camera.position.y, camera.position.z, camera.yaw, camera.pitch };
	int parsed = std::sscanf(spec, "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]);
	if (parsed < 3)
	{
		std::cerr << "camera override: expected \"x,y,z[,yaw,pitch]\", got \"" << spec << "\"" << std::endl;
		return;
	}

	camera.position = glm::vec3(v[0], v[1], v[2]);
	if (parsed >= 5)
	{
		camera.yaw = v[3];
		camera.pitch = v[4];
		glm::vec3 direction;
		direction.x = cos(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));
		direction.y = sin(glm::radians(camera.pitch));
		direction.z = sin(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));
		camera.orientation = glm::normalize(direction);
	}
}
