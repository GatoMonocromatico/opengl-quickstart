#pragma once
// -----------------------------------------------------------------------------
// Light.h — CPU mirror of GLSL Light in default.frag (std140 layout)
// -----------------------------------------------------------------------------
// Keep field order and vec4 packing in sync with your uniform blocks; the GPU
// version is the source of truth for padding rules.

#include <glm/glm.hpp>

struct Light
{
	glm::vec4 position;
	glm::vec4 color;
	glm::vec4 intensity;
};
