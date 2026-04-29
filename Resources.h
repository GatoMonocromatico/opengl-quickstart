#pragma once
// -----------------------------------------------------------------------------
// Resources.h — central bucket for GPU-related assets and scene data
// -----------------------------------------------------------------------------
// This struct is intentionally "wide": shaders, meshes, textures, lights, and
// optional uniform buffers live together so main.cpp and game code have one
// place to pull from. Extend it as your project grows.
#define GLM_ENABLE_EXPERIMENTAL
#include "Mesh.h"
#include "Light.h"
#include "point.h"
#include "animation.h"
#include "texture.h"
#include <vector>
#include <array>

struct Resources
{
	Camera camera;

	// Uniform Buffer Object name for bulk shader data (e.g. std140 light arrays in default.frag).
	GLuint UBO;

	std::vector<Light> scenarioLights;

	// texs[0] and texs[1] are separate lists (e.g. masks vs color atlases) — index convention is yours.
	std::array<std::vector<Texture>, 2> texs;

	std::vector<Animation> anims = std::vector<Animation>{Animation()};
	std::vector< std::vector<Vertex>> verts;
	std::vector<std::vector<GLuint>> indices;

	std::vector<Mesh> meshs;
	std::vector<Point> points;

	std::vector<Shader> shaderProgram;

	// Parses "#RRGGBB" into linear-ish 0..1 RGB with optional brightness tweak; clamps result.
	glm::vec3 hexadecimalToRGB(std::string hex, float saturationOffset = 0.3);
	void load();

	Resources(std::vector<Shader>shds) : shaderProgram(shds)
	{
	}
};
