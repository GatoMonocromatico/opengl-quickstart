#pragma once
// -----------------------------------------------------------------------------
// VAO.h — Vertex Array Object: remembers attrib formats and buffer bindings
// -----------------------------------------------------------------------------
// After setup, Bind() restores every vertex attribute configuration needed to
// draw the mesh, so you do not re-specify pointers each frame.

#include <glad/glad.h>
#include "VBO.h"

class VAO
{
	public:
		GLuint ID;
		VAO();
		void LinkAttrib(VBO& VBO, GLuint layout, GLuint numComponents, GLenum type, GLsizeiptr stride, void* offset, bool instanceAtrib = false);
		void Bind();
		void Unbind();
		void Delete();
};
