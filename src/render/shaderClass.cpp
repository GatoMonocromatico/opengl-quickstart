#include "render/shaderClass.h"
#include "util/DebugLog.h"
#include <stdexcept>
#include <cstring>
#include <vector>

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

// GLSL has no #include, so `#include "file"` lines are expanded here, relative to
// the file doing the including. Every file gets its own #line source number, so a
// compile error like "2(14)" means line 14 of files[2].
static std::string expandIncludes(const std::string& path, std::vector<std::string>& files)
{
	const int sourceIndex = static_cast<int>(files.size());
	files.push_back(path);
	const std::string dir = path.substr(0, path.find_last_of('/') + 1);

	std::istringstream in(getFileContents(path.c_str()));
	// #line can't come before #version, and the top file doesn't need one anyway
	std::string out = sourceIndex > 0 ? "#line 1 " + std::to_string(sourceIndex) + "\n" : "";
	std::string line;
	int lineNo = 0;
	while (std::getline(in, line))
	{
		lineNo++;
		const size_t first = line.find_first_not_of(" \t");
		if (first != std::string::npos && line.compare(first, 8, "#include") == 0)
		{
			const size_t a = line.find('"');
			const size_t b = line.find('"', a + 1);
			out += expandIncludes(dir + line.substr(a + 1, b - a - 1), files);
			out += "#line " + std::to_string(lineNo + 1) + " " + std::to_string(sourceIndex) + "\n";
			continue;
		}
		out += line + "\n";
	}
	return out;
}

static std::string sourceList(const std::vector<std::string>& files)
{
	std::string list;
	for (size_t i = 0; i < files.size(); i++)
		list += "  " + std::to_string(i) + " = " + files[i] + "\n";
	return list;
}

Shader::Shader(const char* vertexFile, const char* fragmentFile)
{
	std::vector<std::string> vertexFiles, fragmentFiles;
	std::string vertexCode = expandIncludes(vertexFile, vertexFiles);
	std::string fragmentCode = expandIncludes(fragmentFile, fragmentFiles);
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
		std::string errorMsg = std::string("Vertex shader compilation failed for '") + vertexFile + "':\n" + infoLog + "sources:\n" + sourceList(vertexFiles);
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
		std::string errorMsg = std::string("Fragment shader compilation failed for '") + fragmentFile + "':\n" + infoLog + "sources:\n" + sourceList(fragmentFiles);
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
	MDBG_IF(flux::verbose, DBG_N("phase", "Shader linked"), DBG_N("vertexFile", vertexFile), DBG_N("fragmentFile", fragmentFile), DBG_N("program_id", ID));
}

void Shader::Activate()
{
	glUseProgram(ID);
}

void Shader::Delete()
{
	glDeleteProgram(ID);
}
