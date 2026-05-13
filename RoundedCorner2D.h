#pragma once

#include "DebugLog.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "Mesh.h"
#include <cmath>

namespace geometry2d
{
	constexpr float kEpsilon = 1e-5f;
	// Rounds one 2D corner on an indexed triangle mesh.
	// Returns false when input/topology is invalid or unsupported.
	bool roundMeshCorner2D(
		std::vector<Vertex>& vertices,
		std::vector<GLuint>& indices,
		GLuint cornerVertexIndex,
		float roundness,
		int roundLineCount
	);

	struct Line 
	{
		double coeficient = 0;
		double constant = 0;
		bool isValid = false;

		Line(GLuint lineEndPoint1, GLuint lineEndPoint2, std::vector<Vertex>& vertices)
		{
			Vertex& EndPoint1 = vertices[lineEndPoint1];
			Vertex& EndPoint2 = vertices[lineEndPoint2];

			coeficient = (EndPoint1.position.y - EndPoint2.position.y) / (double) (EndPoint1.position.x - EndPoint2.position.x);
			constant = std::isinf(coeficient) ? EndPoint1.position.x : coeficient * EndPoint1.position.x - EndPoint1.position.y;

			isValid = lineEndPoint1 != lineEndPoint2;
		}

		bool operator==(Line& l)
		{
			if (std::isinf(l.coeficient) && std::isinf(coeficient))
			{
				return std::abs(l.constant - constant) < kEpsilon && l.isValid && isValid;
			}
			else 
			{
				return std::abs(l.coeficient - coeficient) < kEpsilon && std::abs(l.constant - constant) < kEpsilon && l.isValid && isValid;
			}
		}
	};
}