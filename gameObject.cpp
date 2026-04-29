#include "gameObject.h"
#include "Resources.h"

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
	std::cout << "[GameObject::Draw] begin"
		<< " | meshes_count=" << res.meshs.size()
		<< " | meshIDX=" << meshIDX
		<< " | shader_id=" << shader.ID
		<< std::endl;
	if (meshIDX >= res.meshs.size())
	{
		std::cout << "[GameObject::Draw] skipping draw: invalid meshIDX"
			<< " | meshIDX=" << meshIDX
			<< " | meshes_count=" << res.meshs.size()
			<< std::endl;
		return;
	}

	std::cout << "[GameObject::Draw] selecting mesh"
		<< " | meshIDX=" << meshIDX
		<< " | available_meshes=" << res.meshs.size()
		<< std::endl;
	Mesh& mesh = res.meshs[meshIDX];
	VAO& VAO1 = mesh.VAO1;
	std::cout << "[GameObject::Draw] mesh selected"
		<< " | textures_count=" << mesh.textures.size()
		<< " | indices_count=" << mesh.indices.size()
		<< std::endl;

	std::cout << "[GameObject::Draw] shader activate"
		<< " | shader_id=" << shader.ID
		<< std::endl;
	shader.Activate();
	std::cout << "[GameObject::Draw] vao bind" << std::endl;
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
		std::cout << "[GameObject::Draw] texture setup"
			<< " | i=" << i
			<< " | type=" << type
			<< " | number=" << number
			<< " | slot=" << tex.slot
			<< " | uniform=" << (type + number)
			<< std::endl;
		tex.texUnit(shader, (type + number).c_str(), i);
		std::cout << "[GameObject::Draw] glActiveTexture"
			<< " | slot=" << tex.slot
			<< std::endl;
		glActiveTexture(tex.slot);
		std::cout << "[GameObject::Draw] texture bind"
			<< " | i=" << i
			<< " | slot=" << tex.slot
			<< std::endl;
		tex.Bind();
	}

	// Upload projection * view once per draw (name must match shader uniform: camMatrix).
	std::cout << "[GameObject::Draw] upload camera matrix"
		<< " | uniform=camMatrix"
		<< std::endl;
	camera.Matrix(shader, "camMatrix");

	// World matrix: mesh placement in the level times this object's local transform.
	glm::mat4 resultingModel = mesh.model * model;
	std::cout << "[GameObject::Draw] upload model matrix"
		<< " | uniform=model"
		<< " | shader_id=" << shader.ID
		<< std::endl;
	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(resultingModel));

	// Indexed triangles: EBO bound with VAO supplies index buffer.
	std::cout << "[GameObject::Draw] glDrawElements"
		<< " | mode=GL_TRIANGLES"
		<< " | count=" << mesh.indices.size()
		<< " | type=GL_UNSIGNED_INT"
		<< std::endl;
	glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
}

void GameObject::Draw(Resources& res, Shader& shader, Camera& camera, int numInstances)
{
	std::cout << "[GameObject::DrawInstanced] begin"
		<< " | meshes_count=" << res.meshs.size()
		<< " | meshIDX=" << meshIDX
		<< " | shader_id=" << shader.ID
		<< " | numInstances=" << numInstances
		<< std::endl;
	if (meshIDX >= res.meshs.size())
	{
		std::cout << "[GameObject::DrawInstanced] skipping draw: invalid meshIDX"
			<< " | meshIDX=" << meshIDX
			<< " | meshes_count=" << res.meshs.size()
			<< std::endl;
		return;
	}

	std::cout << "[GameObject::DrawInstanced] selecting mesh"
		<< " | meshIDX=" << meshIDX
		<< " | available_meshes=" << res.meshs.size()
		<< std::endl;
	Mesh& mesh = res.meshs[meshIDX];
	VAO& VAO1 = mesh.VAO1;
	std::cout << "[GameObject::DrawInstanced] mesh selected"
		<< " | textures_count=" << mesh.textures.size()
		<< " | indices_count=" << mesh.indices.size()
		<< std::endl;

	std::cout << "[GameObject::DrawInstanced] shader activate"
		<< " | shader_id=" << shader.ID
		<< std::endl;
	shader.Activate();
	std::cout << "[GameObject::DrawInstanced] vao bind" << std::endl;
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
		std::cout << "[GameObject::DrawInstanced] texture setup"
			<< " | i=" << i
			<< " | type=" << type
			<< " | number=" << number
			<< " | slot=" << tex.slot
			<< " | uniform=" << (type + number)
			<< std::endl;
		tex.texUnit(shader, (type + number).c_str(), i);
		std::cout << "[GameObject::DrawInstanced] glActiveTexture"
			<< " | slot=" << tex.slot
			<< std::endl;
		glActiveTexture(tex.slot);
		std::cout << "[GameObject::DrawInstanced] texture bind"
			<< " | i=" << i
			<< " | slot=" << tex.slot
			<< std::endl;
		tex.Bind();
	}

	std::cout << "[GameObject::DrawInstanced] upload camera matrix"
		<< " | uniform=camMatrix"
		<< std::endl;
	camera.Matrix(shader, "camMatrix");

	glm::mat4 resultingModel = mesh.model * model;
	std::cout << "[GameObject::DrawInstanced] upload model matrix"
		<< " | uniform=model"
		<< " | shader_id=" << shader.ID
		<< std::endl;
	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(resultingModel));

	// Same as glDrawElements but repeats the draw using instanced vertex attributes (divisors on VAO).
	std::cout << "[GameObject::DrawInstanced] glDrawElementsInstanced"
		<< " | mode=GL_TRIANGLES"
		<< " | count=" << mesh.indices.size()
		<< " | type=GL_UNSIGNED_INT"
		<< " | instances=" << numInstances
		<< std::endl;
	glDrawElementsInstanced(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0, numInstances);
}
