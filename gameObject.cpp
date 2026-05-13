#include "gameObject.h"
#include "Resources.h"
#include "DebugLog.h"

void GameObject::translateModel(glm::vec3 translation)
{
	model = glm::translate(model, translation);
}

void GameObject::rotateModel(glm::vec3 axis, float angle)
{
	model = glm::rotate(model, angle, axis);
}

void GameObject::scaleModel(glm::vec3 scale)
{
	model = glm::scale(model, scale);
}

void GameObject::Draw(Resources& res, Shader& shader, Camera& camera)
{
	MDBG(DBG_N("phase", "GameObject::Draw begin"), DBG_N("meshes_count", res.meshs.size()), DBG_N("meshIDX", meshIDX), DBG_N("shader_id", shader.ID));
	if (meshIDX >= res.meshs.size())
	{
		MDBG(DBG_N("phase", "GameObject::Draw skip: invalid meshIDX"), DBG_N("meshIDX", meshIDX), DBG_N("meshes_count", res.meshs.size()));
		return;
	}

	MDBG(DBG_N("phase", "GameObject::Draw selecting mesh"), DBG_N("meshIDX", meshIDX), DBG_N("available_meshes", res.meshs.size()));
	Mesh& mesh = res.meshs[meshIDX];
	VAO& VAO1 = mesh.VAO1;
	MDBG(DBG_N("phase", "GameObject::Draw mesh selected"), DBG_N("textures_count", mesh.textures.size()), DBG_N("indices_count", mesh.indices.size()));

	MDBG(DBG_N("phase", "GameObject::Draw shader activate"), DBG_N("shader_id", shader.ID));
	shader.Activate();
	MDBG("phase", "GameObject::Draw vao bind");
	VAO1.Bind();

	// Assign each texture to a texture unit and set the matching sampler uniform (tex0, mask0, ...).
	unsigned int diffuseNr = 0;
	unsigned int specularNr = 0;

	for (unsigned int i = 0; i < mesh.textures.size(); i++)
	{
		Texture& tex = mesh.textures[i];
		std::string number;
		std::string type = tex.type;
		if (type == "tex")
			number = std::to_string(diffuseNr++);
		else if (type == "mask")
			number = std::to_string(specularNr++);
		MDBG(DBG_N("phase", "GameObject::Draw texture setup"), DBG_N("i", i), DBG_N("type", type), DBG_N("number", number), DBG_N("slot", tex.slot), DBG_N("uniform", (type + number)));
		tex.texUnit(shader, (type + number).c_str(), i);
		MDBG(DBG_N("phase", "GameObject::Draw glActiveTexture"), DBG_N("slot", tex.slot));
		glActiveTexture(tex.slot);
		MDBG(DBG_N("phase", "GameObject::Draw texture bind"), DBG_N("i", i), DBG_N("slot", tex.slot));
		tex.Bind();
	}

	// Upload projection * view once per draw (name must match shader uniform: camMatrix).
	MDBG(DBG_N("phase", "GameObject::Draw upload camera matrix"), DBG_N("uniform", "camMatrix"));
	camera.Matrix(shader, "camMatrix");

	// World matrix: mesh placement in the level times this object's local transform.
	glm::mat4 resultingModel = mesh.model * model;
	MDBG(DBG_N("phase", "GameObject::Draw upload model matrix"), DBG_N("uniform", "model"), DBG_N("shader_id", shader.ID));
	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(resultingModel));

	// Indexed triangles: EBO bound with VAO supplies index buffer.
	MDBG(DBG_N("phase", "GameObject::Draw glDrawElements"), DBG_N("mode", "GL_TRIANGLES"), DBG_N("count", mesh.indices.size()), DBG_N("type", "GL_UNSIGNED_INT"));
	glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
}

void GameObject::Draw(Resources& res, Shader& shader, Camera& camera, int numInstances)
{
	MDBG(DBG_N("phase", "GameObject::DrawInstanced begin"), DBG_N("meshes_count", res.meshs.size()), DBG_N("meshIDX", meshIDX), DBG_N("shader_id", shader.ID), DBG_N("numInstances", numInstances));
	if (meshIDX >= res.meshs.size())
	{
		MDBG(DBG_N("phase", "GameObject::DrawInstanced skip: invalid meshIDX"), DBG_N("meshIDX", meshIDX), DBG_N("meshes_count", res.meshs.size()));
		return;
	}

	MDBG(DBG_N("phase", "GameObject::DrawInstanced selecting mesh"), DBG_N("meshIDX", meshIDX), DBG_N("available_meshes", res.meshs.size()));
	Mesh& mesh = res.meshs[meshIDX];
	VAO& VAO1 = mesh.VAO1;
	MDBG(DBG_N("phase", "GameObject::DrawInstanced mesh selected"), DBG_N("textures_count", mesh.textures.size()), DBG_N("indices_count", mesh.indices.size()));

	MDBG(DBG_N("phase", "GameObject::DrawInstanced shader activate"), DBG_N("shader_id", shader.ID));
	shader.Activate();
	MDBG("phase", "GameObject::DrawInstanced vao bind");
	VAO1.Bind();

	unsigned int diffuseNr = 0;
	unsigned int specularNr = 0;

	for (unsigned int i = 0; i < mesh.textures.size(); i++)
	{
		Texture& tex = mesh.textures[i];
		std::string number;
		std::string type = tex.type;
		if (type == "tex")
			number = std::to_string(diffuseNr++);
		else if (type == "mask")
			number = std::to_string(specularNr++);
		MDBG(DBG_N("phase", "GameObject::DrawInstanced texture setup"), DBG_N("i", i), DBG_N("type", type), DBG_N("number", number), DBG_N("slot", tex.slot), DBG_N("uniform", (type + number)));
		tex.texUnit(shader, (type + number).c_str(), i);
		MDBG(DBG_N("phase", "GameObject::DrawInstanced glActiveTexture"), DBG_N("slot", tex.slot));
		glActiveTexture(tex.slot);
		MDBG(DBG_N("phase", "GameObject::DrawInstanced texture bind"), DBG_N("i", i), DBG_N("slot", tex.slot));
		tex.Bind();
	}

	MDBG(DBG_N("phase", "GameObject::DrawInstanced upload camera matrix"), DBG_N("uniform", "camMatrix"));
	camera.Matrix(shader, "camMatrix");

	glm::mat4 resultingModel = mesh.model * model;
	MDBG(DBG_N("phase", "GameObject::DrawInstanced upload model matrix"), DBG_N("uniform", "model"), DBG_N("shader_id", shader.ID));
	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(resultingModel));

	// Same as glDrawElements but repeats the draw using instanced vertex attributes (divisors on VAO).
	MDBG(DBG_N("phase", "GameObject::DrawInstanced glDrawElementsInstanced"), DBG_N("mode", "GL_TRIANGLES"), DBG_N("count", mesh.indices.size()), DBG_N("type", "GL_UNSIGNED_INT"), DBG_N("instances", numInstances));
	glDrawElementsInstanced(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0, numInstances);
}
