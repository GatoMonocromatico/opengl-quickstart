#include "Resources.h"
#include <cmath>
#include <algorithm>

// -----------------------------------------------------------------------------
// hexadecimalToRGB — tiny hex color parser for CPU-side glm::vec3 colors
// -----------------------------------------------------------------------------
// Each pair of hex digits becomes one channel: R, G, B. Optional offset bumps
// values before clamping to [0,1] (naming in code is historical: "saturation").
glm::vec3 Resources::hexadecimalToRGB(std::string hex, float saturationOffset)
{
	glm::vec3 rgb(0);

	if (hex[0] == '#')
	{
		hex.erase(hex.begin());
	}

	// Two hex digits per channel: high nibble * 16 + low nibble, normalized to 0..1.
	for (int i = 0; i < 3; i++)
	{
		for (int hexadecimalPlace = 0; hexadecimalPlace < 2; hexadecimalPlace++)
		{
			switch (hex[i * 2 + hexadecimalPlace])
			{
			case '0':
				rgb[i] += 0.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '1':
				rgb[i] += 1.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '2':
				rgb[i] += 2.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '3':
				rgb[i] += 3.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '4':
				rgb[i] += 4.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '5':
				rgb[i] += 5.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '6':
				rgb[i] += 6.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '7':
				rgb[i] += 7.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '8':
				rgb[i] += 8.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case '9':
				rgb[i] += 9.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case 'a':
				rgb[i] += 10.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case 'b':
				rgb[i] += 11.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case 'c':
				rgb[i] += 12.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case 'd':
				rgb[i] += 13.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case 'e':
				rgb[i] += 14.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			case 'f':
				rgb[i] += 15.0f * powf(16.0f, 1 - hexadecimalPlace) / 255.0f + saturationOffset;
				break;
			}
		}
	}

	for (int i = 0; i < 3; i++)
	{
		rgb[i] = std::min(rgb[i], 1.0f);
		rgb[i] = std::max(rgb[i], 0.0f);
	}

	return rgb;
}

// -----------------------------------------------------------------------------
// Resources::load — populate vectors, meshes, lights, UBOs (your scene setup)
// -----------------------------------------------------------------------------
// The block below is a commented walkthrough: uncomment and adapt paths/indices
// when you are ready to draw. It shows how data flows from CPU vectors into
// Mesh/Point objects and OpenGL buffer setup.
void Resources::load()
{

	// --- Example: build one textured quad-like mesh, optional point primitive, lights, UBO. ---

	// --- Vertices (must match layout in Mesh constructor / default.vert locations) ---
	verts.push_back(std::vector<Vertex>{
		// position              color (often unused if textured)   diffuse UV   mask UV       normal (filled further by Mesh::createNormals)   per-vertex "glow" scalar
		Vertex{ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), 1.0f },
		Vertex{ glm::vec3(50.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), 1.0f },
		Vertex{ glm::vec3(50.0f, 50.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.0f), 1.0f },
		Vertex{ glm::vec3(0.0f, 50.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), 1.0f },
	});

	// --- Indices: two triangles (CCW winding is typical for front faces) ---
	indices.push_back(std::vector<GLuint>{ 1, 0, 2, 3, 2, 0 });

	// --- Textures: loaded from disk; "type" strings must match what GameObject::Draw expects ("tex", "mask") ---
	texs[0].push_back(Texture("Textures/no-mask.png", "mask", GL_TEXTURE0, GL_RGBA, GL_UNSIGNED_BYTE));
	texs[1].push_back(Texture("Textures/block.png", "tex", GL_TEXTURE1, GL_RGBA, GL_UNSIGNED_BYTE));

	Mesh blockMesh = Mesh(verts[0], indices[0], std::vector<Texture>{ texs[0][0], texs[1][0] });

	meshs.push_back(blockMesh);
	
	/*
	// --- Point primitive (uses point.vert / point.frag and GL_POINTS) ---
	verts.push_back(std::vector<Vertex>{texture
		Vertex{ glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), 1.0f },
	});
	Point point = Point(verts[1]);
	points.push_back(point);
	*/

	// camera.rotateCamera(glm::vec3(1.0f, 0.0f, 0.0f), glm::radians(-8.0f));

	shaderProgram[0].Activate();

	glGenBuffers(1, &UBO);
	glBindBuffer(GL_UNIFORM_BUFFER, UBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(Light) * 64, NULL, GL_DYNAMIC_DRAW);
	// Block name must match default.frag (e.g. "ScenarioLights" or "BrickLights"), not a made-up name.
	GLuint lightBlockIndex1 = glGetUniformBlockIndex(shaderProgram[0].ID, "ScenarioLights");
	glUniformBlockBinding(shaderProgram[0].ID, lightBlockIndex1, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, UBO);
	if (lightBlockIndex1 == GL_INVALID_INDEX)
	{
		std::cout << "Lights uniform block not found in shader!" << std::endl;
	}

	scenarioLights.push_back(Light{ glm::vec4(0.55f - 0.775f, 0.0f, 0.4f, 0.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f) });
}
