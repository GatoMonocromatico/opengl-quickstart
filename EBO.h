#pragma once
// -----------------------------------------------------------------------------
// EBO.h — Element Buffer Object: GPU index buffer for indexed drawing
// -----------------------------------------------------------------------------
// Indices describe which vertices form each triangle. glDrawElements uses the
// bound GL_ELEMENT_ARRAY_BUFFER (this EBO) together with a bound VAO.

#include <glad/glad.h>
#include <vector>

class EBO
{
public:
	GLuint ID;
	EBO();
	void Bind();
	void Unbind();
	void Delete();
	void createEBO(std::vector<GLuint>& indices);
};
