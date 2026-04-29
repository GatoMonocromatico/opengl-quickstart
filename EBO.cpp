#include "EBO.h"

EBO::EBO()
{
	// Allocate one buffer name; data upload happens in createEBO.
	glGenBuffers(1, &ID);
}

void EBO::createEBO(std::vector<GLuint>& indices)
{
	// GL_ELEMENT_ARRAY_BUFFER is stored in the currently bound VAO when one is active.
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
}

void EBO::Bind()
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ID);
}

void EBO::Unbind()
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void EBO::Delete()
{
	glDeleteBuffers(1, &ID);
}
