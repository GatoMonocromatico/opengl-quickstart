#include "shaderClass.h"
#include <stdexcept>
#include <cstring>

std::string getFileContents(const char* filename)
{
	std::ifstream in(filename, std::ios::binary);
	if (in)
	{
		std::string contents;
		in.seekg(0, std::ios::end);
		contents.resize(in.tellg());
		in.seekg(0, std::ios::beg);
		in.read(&contents[0], contents.size());
		in.close();
		return(contents);
	}
	const int err = errno;
	const char* errMsg = std::strerror(err);
	throw std::runtime_error(
		std::string("Failed to read shader file '") + filename + "': " +
		(errMsg ? errMsg : "unknown I/O error")
	);
}

Shader::Shader(const char* vertexFile, const char* fragmentFile)
{
	std::string vertexCode = getFileContents(vertexFile);
	std::string fragmentCode = getFileContents(fragmentFile);
	const char* vertexSource = vertexCode.c_str();
	const char* fragmentSource = fragmentCode.c_str();

	GLuint vertexShader, fragmentShader;
	// Compile vertex stage (runs once per vertex).
	vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexSource, NULL);
	glCompileShader(vertexShader);
	GLint success;
	GLchar infoLog[512];
	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
		std::string errorMsg = std::string("Vertex shader compilation failed for '") + vertexFile + "':\n" + infoLog;
		std::cerr << errorMsg << std::endl;
		glDeleteShader(vertexShader);
		throw std::runtime_error(errorMsg);
	}
	// Compile fragment stage (runs once per rasterized pixel/sample).
	fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentSource, NULL);
	glCompileShader(fragmentShader);
	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
		std::string errorMsg = std::string("Fragment shader compilation failed for '") + fragmentFile + "':\n" + infoLog;
		std::cerr << errorMsg << std::endl;
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		throw std::runtime_error(errorMsg);
	}
	// Link into a program: matches in/out varyings must line up between stages.
	ID = glCreateProgram();
	glAttachShader(ID, vertexShader);
	glAttachShader(ID, fragmentShader);
	glLinkProgram(ID);
	glGetProgramiv(ID, GL_LINK_STATUS, &success);
	if (!success)
	{
		glGetProgramInfoLog(ID, 512, NULL, infoLog);
		std::string errorMsg = std::string("Shader program linking failed (") + vertexFile + " + " + fragmentFile + "):\n" + infoLog;
		std::cerr << errorMsg << std::endl;
		glDeleteProgram(ID);
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		throw std::runtime_error(errorMsg);
	}
	// Detach/delete shaders after link; the program keeps the compiled code.
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
}

void Shader::Activate()
{
	glUseProgram(ID);
}

void Shader::Delete()
{
	glDeleteProgram(ID);
}
