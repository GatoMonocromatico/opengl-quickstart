#define GLM_ENABLE_EXPERIMENTAL
#include "Mesh.h"
#include <iostream>

Mesh::Mesh(std::vector<Vertex>& verts, std::vector<GLuint>& indxs, std::vector<Texture> textures, glm::vec3 model) :
	vertices(verts),
	indices(indxs),
	textures(textures),
	VBO1(VBO()),
	VBOInstances(VBO()),
	EBO1(EBO()),
	model(glm::translate(glm::mat4(1.0f), model))
{
	// Normals default to 0; this duplicates vertices on sharp edges so lighting matches faceted art.
	createNormals();

	VAO1.Bind();

	VBO1.createVBO(vertices);
	EBO1.createEBO(indices);

	// Vertex layout must match struct Vertex and default.vert attribute locations (stride = sizeof(Vertex)).
	VAO1.LinkAttrib(VBO1, 0, 3, GL_FLOAT, sizeof(Vertex), (void*)0);                          // position
	VAO1.LinkAttrib(VBO1, 1, 3, GL_FLOAT, sizeof(Vertex), (void*)(3 * sizeof(float)));         // color
	VAO1.LinkAttrib(VBO1, 2, 2, GL_FLOAT, sizeof(Vertex), (void*)(6 * sizeof(float)));         // diffuse UV
	VAO1.LinkAttrib(VBO1, 3, 2, GL_FLOAT, sizeof(Vertex), (void*)(8 * sizeof(float)));         // mask UV
	VAO1.LinkAttrib(VBO1, 4, 3, GL_FLOAT, sizeof(Vertex), (void*)(10 * sizeof(float)));        // normal
	VAO1.LinkAttrib(VBO1, 7, 1, GL_FLOAT, sizeof(Vertex), (void*)(13 * sizeof(float)));       // naturalLuminence (per-vertex scalar)

	// Placeholder per-instance data; updateInstanceVBO can stream real offsets each frame.
	VBOInstances.createVBO(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});

	VAO1.LinkAttrib(VBOInstances, 5, 3, GL_FLOAT, 5 * sizeof(float), (void*)0);             // instance position offset
	VAO1.LinkAttrib(VBOInstances, 6, 2, GL_FLOAT, 5 * sizeof(float), (void*)(3 * sizeof(float))); // instance UV offset

	// Divisor 1: advance these attributes once per instance, not per vertex.
	glVertexAttribDivisor(5, 1);
	glVertexAttribDivisor(6, 1);

	VAO1.Unbind();
	VBO1.Unbind();
	VBOInstances.Unbind();
	EBO1.Unbind();
}

void Mesh::createNormals()
{
	/* Indexing strategy for this helper:
	   [same/different normal][how many vertices overlap between two triangles - 1 or 2]
	   Used to decide whether triangles share a smooth edge or need a split for flat shading.
	*/
	std::vector<std::vector<std::vector<GLuint>>> idxTrianglesRepeating;
	idxTrianglesRepeating.resize(2);
	idxTrianglesRepeating[0].resize(2);
	idxTrianglesRepeating[1].resize(2);
	size_t readIndex = 0;

	// Walk the index list in groups of three (one triangle at a time).
	while (indices.size() > readIndex + 2)
	{
		idxTrianglesRepeating[0][0].clear();
		idxTrianglesRepeating[0][1].clear();
		idxTrianglesRepeating[1][0].clear();
		idxTrianglesRepeating[1][1].clear();

		GLuint& comparsionIndex1 = indices[readIndex];
		GLuint& comparsionIndex2 = indices[readIndex + 1];
		GLuint& comparsionIndex3 = indices[readIndex + 2];
		glm::vec3 comparsionV1 = vertices[comparsionIndex1].position;
		glm::vec3 comparsionV2 = vertices[comparsionIndex2].position;
		glm::vec3 comparsionV3 = vertices[comparsionIndex3].position;
		glm::vec3 comparsionNormal = glm::normalize(glm::cross(comparsionV2 - comparsionV1, comparsionV3 - comparsionV1));

		for (size_t i = readIndex + 3; i < indices.size(); i += 3)
		{
			GLuint& index1 = indices[i];
			GLuint& index2 = indices[i + 1];
			GLuint& index3 = indices[i + 2];

			glm::vec3 v1 = vertices[index1].position;
			glm::vec3 v2 = vertices[index2].position;
			glm::vec3 v3 = vertices[index3].position;

			glm::vec3 normal = glm::normalize(glm::cross(v2 - v1, v3 - v1));

			int numRepeatedVertices =
				(comparsionIndex1 == index1) + (comparsionIndex1 == index2) + (comparsionIndex1 == index3) +
				(comparsionIndex2 == index1) + (comparsionIndex2 == index2) + (comparsionIndex2 == index3) +
				(comparsionIndex3 == index1) + (comparsionIndex3 == index2) + (comparsionIndex3 == index3);

			if (numRepeatedVertices > 2)
			{
				std::cout << "Error: More than 2 repeated vertices in triangle, can't calculate normal" << std::endl;
			}
			else if (numRepeatedVertices != 0)
			{
				idxTrianglesRepeating[normal == comparsionNormal][numRepeatedVertices - 1].push_back(i);
			}
		}


		std::vector<GLuint> indicesToChange;

		for (size_t numRepeatVertex = 0; numRepeatVertex < 2; numRepeatVertex += 1)
		{
			for (size_t J1 = 0; J1 < idxTrianglesRepeating[0][numRepeatVertex].size(); J1++)
			{
				size_t j = 0;

				for (j; j < 3; j += 1)
				{
					for (size_t k = 0; k < 3; k += 1)
					{
						if (indices[readIndex + j] == indices[idxTrianglesRepeating[0][numRepeatVertex][J1] + k])
						{
							vertices.push_back(vertices[indices[readIndex + j]]);
							indicesToChange.push_back(indices[readIndex + j]);
							indices[readIndex + j] = vertices.size() - 1;

						}
					}
				}

				for (size_t numRepeatVertex = 0; numRepeatVertex < 2; numRepeatVertex += 1)
				{
					for (size_t I1 = 0; I1 < idxTrianglesRepeating[1][numRepeatVertex].size(); I1++)
					{
						size_t i = 0;
						for (i; i < 3; i += 1)
						{
							for (size_t I2 = 0; I2 < indicesToChange.size(); I2++)
							{
								if (indicesToChange[I2] == indices[idxTrianglesRepeating[1][numRepeatVertex][I1] + i])
								{
									indices[idxTrianglesRepeating[1][numRepeatVertex][I1] + i] = vertices.size() - 1;
								}
							}

						}
					}
				}
				indicesToChange.clear();
			}
		}


		vertices[comparsionIndex1].normal = comparsionNormal;
		vertices[comparsionIndex2].normal = comparsionNormal;
		vertices[comparsionIndex3].normal = comparsionNormal;

		readIndex += 3;
	}

	std::cout << "vertices: " << vertices.size() << std::endl;

	/*for (int i = 0; i < indices.size(); i++)
	{
		std::cout << vertices[indices[i]].normal.x << ", " << vertices[indices[i]].normal.y << ", " << vertices[indices[i]].normal.z << "\n";
		i += 1;
		std::cout << vertices[indices[i]].normal.x << ", " << vertices[indices[i]].normal.y << ", " << vertices[indices[i]].normal.z << "\n";
		i += 1;
		std::cout << vertices[indices[i]].normal.x << ", " << vertices[indices[i]].normal.y << ", " << vertices[indices[i]].normal.z << "\n\n";
	}*/
}

void Mesh::Draw(Shader& shader, Camera& camera, glm::mat4 objectModel)
{
	shader.Activate();
	VAO1.Bind();

	unsigned int diffuseNr = 0;
	unsigned int specularNr = 0;

	for (unsigned int i = 0; i < textures.size(); i++)
	{
		Texture& tex = textures[i];
		std::string number;
		std::string type = tex.type;
		if (type == "tex")
			number = std::to_string(diffuseNr++);
		else if (type == "mask")
			number = std::to_string(specularNr++);
		tex.texUnit(shader, (type + number).c_str(), i);
		glActiveTexture(tex.slot);
		tex.Bind();
	}

	camera.Matrix(shader, "camMatrix");

	glm::mat4 resultingModel = model * objectModel;
	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(resultingModel));

	glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
}

void Mesh::updateInstanceVBO(std::vector<float>& data)
{
	VBOInstances.Bind();
	glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STREAM_DRAW);
	VBOInstances.Unbind();
}

void Mesh::translateModel(glm::vec3 translation)
{
	model = glm::translate(model, translation);
}

void Mesh::rotateModel(glm::vec3 axis, float angle)
{
	model = glm::rotate(model, angle, axis);
}

void Mesh::scaleModel(glm::vec3 scale)
{
	model = glm::scale(model, scale);
}

void Mesh::Draw(Shader& shader, Camera& camera, glm::mat4 objectModel, int numInstances)
{
	shader.Activate();
	VAO1.Bind();

	unsigned int diffuseNr = 0;
	unsigned int specularNr = 0;

	for (unsigned int i = 0; i < textures.size(); i++)
	{
		Texture& tex = textures[i];
		std::string number;
		std::string type = tex.type;
		if (type == "tex")
			number = std::to_string(diffuseNr++);
		else if (type == "mask")
			number = std::to_string(specularNr++);
		tex.texUnit(shader, (type + number).c_str(), i);
		glActiveTexture(tex.slot);
		tex.Bind();
	}

	camera.Matrix(shader, "camMatrix");

	glm::mat4 resultingModel = model * objectModel;
	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(resultingModel));

	glDrawElementsInstanced(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0, numInstances);
}
