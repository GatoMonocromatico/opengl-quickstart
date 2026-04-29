#pragma once
// -----------------------------------------------------------------------------
// Mesh.h — geometry + textures + VAO wiring (optional instancing attributes)
// -----------------------------------------------------------------------------
// Constructing a Mesh uploads vertices to a VBO, indices to an EBO, and stores
// vertex attribute layout in a VAO so draw calls are just Bind + glDrawElements.

#include <string>
#include "VAO.h"
#include "EBO.h"
#include "Camera.h"
#include "texture.h"

class Mesh
{
	public:
		std::vector<Vertex> vertices;
		std::vector<GLuint> indices;
		std::vector<Texture> textures;

		VBO VBO1;
		VBO VBOInstances;
		EBO EBO1;

		// Base transform for this mesh in world space (object-level transforms multiply on top).
		glm::mat4 model;


		VAO VAO1;
	Mesh(std::vector<Vertex>& vertices, std::vector<GLuint>& indices, std::vector<Texture> textures, glm::vec3 model = glm::vec3(0.0f, 0.0f, 0.0f));
	void Draw(Shader& shader, Camera& camera, glm::mat4 objectModel = glm::mat4(1));
	void Draw(Shader& shader, Camera& camera, glm::mat4 objectModel = glm::mat4(1), int numIntaces = 1);
	void updateInstanceVBO(std::vector<float>& data);
	void translateModel(glm::vec3 translation);
	void rotateModel(glm::vec3 axis, float angle);
	void scaleModel(glm::vec3 scale);
	// Splits shared vertices so each flat face can own a single per-face normal (lighting).
	void createNormals();
	void Delete()
	{
		VAO1.Delete();
		VBO1.Delete();
		VBOInstances.Delete();
		EBO1.Delete();
		for (Texture& texture : textures)
		{
			texture.Delete();
		}
	}

};
