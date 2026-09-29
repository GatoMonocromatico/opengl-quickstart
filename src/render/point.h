#pragma once
// -----------------------------------------------------------------------------
// point.h — minimal geometry path for GL_POINTS (debug markers, particles, etc.)
// -----------------------------------------------------------------------------
// Uses the same Vertex layout as meshes so attribute locations stay consistent,
// but drawing is glDrawArrays(GL_POINTS) instead of indexed triangles.

#include "util/DebugLog.h"
#include <string>
#include "render/VAO.h"
#include "render/EBO.h"
#include "render/camera.h"
#include "render/texture.h"

class Point
{
public:
	std::vector<Vertex> vertices;

	VBO VBO1;
	VBO VBOInstances;

	glm::mat4 model;


	VAO VAO1;
	Point(std::vector<Vertex>& vertices, glm::vec3 model = glm::vec3(0.0f, 0.0f, 0.0f));
	void Draw(Shader& shader, Camera& camera);
	void updateInstanceVBO(std::vector<float>& data);
	void translateModel(glm::vec3 translation);
	void rotateModel(glm::vec3 axis, float angle);
	void Delete()
	{
		VAO1.Delete();
		VBO1.Delete();
		VBOInstances.Delete();
	}

};
