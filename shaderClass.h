#pragma once
// -----------------------------------------------------------------------------
// shaderClass.h — compile/link GLSL stages into one program object
// -----------------------------------------------------------------------------
// Shader::ID is the OpenGL program name used with glUseProgram.

#include <glad/glad.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cerrno>

std::string getFileContents(const char* filename);

class Shader
{
	public:
		GLuint ID;
		Shader(const char* vertexFile, const char* fragmentFile);
		void Activate();
		void Delete();
};
