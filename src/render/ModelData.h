#pragma once
// -----------------------------------------------------------------------------
// ModelData.h — read 3D model files into plain vertex data (no OpenGL)
// -----------------------------------------------------------------------------
// Formats, picked by extension:
//   .gltf / .glb  glTF 2.0 via cgltf: every triangle primitive of the default scene, with node
//                 transforms baked into the vertices; per-vertex normals and COLOR_0 when
//                 present, otherwise smooth normals and the material's base colour. Textures
//                 are not loaded yet.
//   .stl          binary or ASCII; one light-grey part with facet normals.
// Units are the file's own (glTF: metres, +Y up; STL: often millimetres). render/Model.h
// uploads the result as Meshes. Kept GL-free so it can be tested headless.

#include "render/VBO.h"

#include <string>
#include <vector>

struct ModelPart
{
	std::vector<Vertex> vertices; // colour alpha 1, so the colour covers any texture fully
	std::vector<GLuint> indices;  // triangles
};

struct ModelData
{
	std::vector<ModelPart> parts;
	glm::vec3 boundsMin{0.0f};
	glm::vec3 boundsMax{0.0f};
};

// Fills `out`. On failure returns false and sets `error`.
bool loadModelData(const std::string& path, ModelData& out, std::string& error);
