#define GLM_ENABLE_EXPERIMENTAL
#include "render/Model.h"

bool Model::load(const std::string& path, std::string& error)
{
	ModelData data;
	if (!loadModelData(path, data, error))
		return false;
	Delete();
	for (const ModelPart& part : data.parts)
		meshes.push_back(std::make_unique<Mesh>(part.vertices, part.indices, std::vector<Texture>{}, glm::vec3(0.0f), false));
	boundsMin = data.boundsMin;
	boundsMax = data.boundsMax;
	return true;
}

void Model::Draw(Shader& shader, Camera& camera, glm::mat4 objectModel)
{
	for (auto& mesh : meshes)
		mesh->Draw(shader, camera, objectModel, 1);
}

void Model::Delete()
{
	for (auto& mesh : meshes)
		mesh->Delete();
	meshes.clear();
}
