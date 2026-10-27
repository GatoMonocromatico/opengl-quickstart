#pragma once
// -----------------------------------------------------------------------------
// Model.h — a loaded 3D model (.gltf, .glb, .stl) as GPU meshes
// -----------------------------------------------------------------------------
// Model::load reads the file with loadModelData (render/ModelData.h) and uploads one Mesh per
// part, keeping the file's normals. Shaders get position, colour (rgba) and normal like any
// Mesh; there are no textures yet.
//
//   Model drone;
//   std::string err;
//   if (!drone.load("assets/drone.glb", err)) std::cerr << err << "\n";
//   drone.Draw(shader, camera, transform);

#include "render/Mesh.h"
#include "render/ModelData.h"

#include <memory>
#include <string>
#include <vector>

class Model
{
public:
	std::vector<std::unique_ptr<Mesh>> meshes;
	glm::vec3 boundsMin{0.0f};
	glm::vec3 boundsMax{0.0f};

	// Replaces whatever was loaded. On failure returns false and sets `error`.
	bool load(const std::string& path, std::string& error);
	void Draw(Shader& shader, Camera& camera, glm::mat4 objectModel = glm::mat4(1.0f));
	void Delete();
};
