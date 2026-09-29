#include "render/VAO.h"
#include "util/DebugLog.h"

VAO::VAO()
{
	glGenVertexArrays(1, &ID);
	MDBG_IF(flux::verbose, DBG_N("phase", "VAO::VAO glGenVertexArrays"));
}

void VAO::LinkAttrib(VBO& VBO, GLuint layout, GLuint numComponents, GLenum type, GLsizeiptr stride, void* offset, bool instanceAtrib)
{
	VBO.Bind();
	// Describe how to walk the bound GL_ARRAY_BUFFER for attribute `layout`.
	glVertexAttribPointer(layout, numComponents, type, GL_FALSE, stride, offset);
	glEnableVertexAttribArray(layout);

	if (instanceAtrib) glVertexAttribDivisor(layout, 1);
	VBO.Unbind();
}

void VAO::Bind()
{
	glBindVertexArray(ID);
}

void VAO::Unbind()
{
	glBindVertexArray(0);
}

void VAO::Delete()
{
	glDeleteVertexArrays(1, &ID);
}
