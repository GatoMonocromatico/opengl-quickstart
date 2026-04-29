#define GLM_ENABLE_EXPERIMENTAL

#include "camera.h"
#include <algorithm>

void Camera::updateMatrix(float FOVdeg, float nearPlane, float farPlane, int width, int height)
{
	const int safeWidth = std::max(width, 10);
	const int safeHeight = std::max(height, 10);
	// lookAt builds the inverse of the camera's rigid body transform in world space.
	glm::mat4 view = glm::lookAt(position, position + orientation, up);
	// Perspective divide happens in hardware after the vertex shader; w stores depth scaling.
	glm::mat4 proj = glm::perspective(glm::radians(FOVdeg), (float)safeWidth / (float)safeHeight, nearPlane, farPlane);
	cameraMatrix = proj * view;
}

void Camera::Matrix(Shader& shader, const char* uniform)
{
	glUniformMatrix4fv(glGetUniformLocation(shader.ID, uniform), 1, GL_FALSE, glm::value_ptr(cameraMatrix));

}

void Camera::rotateCamera(glm::vec3 axis, float angle)
{
	orientation = glm::rotate(orientation, angle, axis);
}
