#define GLM_ENABLE_EXPERIMENTAL
#include "point.h"


void Point::Draw(Shader& shader, Camera& camera)
{
	shader.Activate();
	VAO1.Bind();

	camera.Matrix(shader, "camMatrix");

	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "model"), 1, GL_FALSE, glm::value_ptr(model));

	// One draw call issues `vertices.size()` points; point size comes from the vertex shader.
	glDrawArrays(GL_POINTS, 0, vertices.size());
}

Point::Point(std::vector<Vertex>& verts, glm::vec3 model) :
	vertices(verts),
	VBO1(VBO()),
	VBOInstances(VBO()),
	model(glm::translate(glm::mat4(1.0f), model))
{
	VAO1.Bind();

	// Same stride/offsets as Mesh so one mental model for attribute packing.
	VAO1.LinkAttrib(VBO1, 0, 3, GL_FLOAT, sizeof(Vertex), (void*)0);
	VAO1.LinkAttrib(VBO1, 1, 3, GL_FLOAT, sizeof(Vertex), (void*)(3 * sizeof(float)));
	VAO1.LinkAttrib(VBO1, 2, 2, GL_FLOAT, sizeof(Vertex), (void*)(6 * sizeof(float)));
	VAO1.LinkAttrib(VBO1, 3, 2, GL_FLOAT, sizeof(Vertex), (void*)(8 * sizeof(float)));
	VAO1.LinkAttrib(VBO1, 4, 3, GL_FLOAT, sizeof(Vertex), (void*)(10 * sizeof(float)));
	VAO1.LinkAttrib(VBO1, 7, 1, GL_FLOAT, sizeof(Vertex), (void*)(13 * sizeof(float)));

	VBOInstances.createVBO(std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f});

	VAO1.LinkAttrib(VBOInstances, 5, 3, GL_FLOAT, 5 * sizeof(float), (void*)0);
	VAO1.LinkAttrib(VBOInstances, 6, 2, GL_FLOAT, 5 * sizeof(float), (void*)(3 * sizeof(float)));

	glVertexAttribDivisor(5, 1);
	glVertexAttribDivisor(6, 1);

	VAO1.Unbind();
	VBO1.Unbind();
	VBOInstances.Unbind();
}


void Point::updateInstanceVBO(std::vector<float>& data)
{
	VBOInstances.Bind();
	glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STREAM_DRAW);
	VBOInstances.Unbind();
}

void Point::translateModel(glm::vec3 translation)
{
	model = glm::translate(model, translation);
}

void Point::rotateModel(glm::vec3 axis, float angle)
{
	model = glm::rotate(model, angle, axis);
}
