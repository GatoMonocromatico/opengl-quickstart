#pragma once
// -----------------------------------------------------------------------------
// gameObject.h — logical entity: mesh reference + local transform + draw helpers
// -----------------------------------------------------------------------------
// A GameObject does not own GPU buffers; it indexes into Resources::meshs and
// multiplies its own model matrix with the mesh's base model matrix at draw time.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <vector>
#include <SDL3/SDL.h>
#include <array>
#include <unordered_set>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "Camera.h"
#include "texture.h"
#include "animation.h"
#include "shaderClass.h"
#include "Resources.h"

enum class ObjectType {
	brickUnit, level, grid
};

struct GameObject
{
	size_t specificDataLocation;
	ObjectType type;

	std::vector<Animation> anims;
	int32_t currentAnimation;
	size_t meshIDX;

	// Local transform relative to the mesh's own model matrix in Resources.
	glm::mat4 model;

	void Draw(Resources& res, Shader& shader, Camera& camera);
	void Draw(Resources& res, Shader& shader, Camera& camera, int numIntaces);

	void translateModel(glm::vec3 translation);
	void rotateModel(glm::vec3 axis, float angle);
	void scaleModel(glm::vec3 scale);

	GameObject(ObjectType&& t, size_t loc, size_t mIdx, std::vector<Animation> anim = std::vector<Animation>{Animation()}, int32_t CA = 0) :
		type(t),
		specificDataLocation(loc),
		anims(anim),
		currentAnimation(CA),
		meshIDX(mIdx),
		model(glm::mat4(1))
	{
	}
	GameObject(ObjectType&& t, size_t loc, size_t mIdx, glm::mat4 m, std::vector<Animation> anim = std::vector<Animation>{Animation()}, int32_t CA = 0) :
		type(t),
		specificDataLocation(loc),
		anims(anim),
		currentAnimation(CA),
		meshIDX(mIdx),
		model(m)
	{
	}
};
