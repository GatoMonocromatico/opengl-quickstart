#pragma once

#include "util/DebugLog.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "render/Mesh.h"
#include <cmath>
#include <vector>


namespace geometry2d
{
	constexpr float kEpsilon = 1e-5f;
	// Rounds one 2D corner on an indexed triangle mesh.
	// Returns false when input/topology is invalid or unsupported.
	bool roundMeshCorner2D(
		std::vector<Vertex>& vertices,
		std::vector<GLuint>& indices,
		GLuint cornerVertexIndex,
		float shortenDistance,
		int roundLineCount
	);

	struct parametricDescription{
		double coeficient = 0;
		double constant = 0;
	};

	struct Line
	{
		glm::vec2 direction{};
		std::vector<parametricDescription> parametricEquations{};

		Line(GLuint lineEndPoint1, GLuint lineEndPoint2, std::vector<Vertex>& vertices)
		{
			parametricEquations.resize(2);
			Vertex& EndPoint1 = vertices[lineEndPoint1];
			Vertex& EndPoint2 = vertices[lineEndPoint2];

			direction.x = (EndPoint1.position.x - EndPoint2.position.x);
			direction.y = (EndPoint1.position.y - EndPoint2.position.y);

			direction = glm::normalize(direction);

			if (std::abs(std::abs(direction.y) - 1) < kEpsilon)
			{
				parametricEquations[0].constant = EndPoint1.position.x;
			}
			else if (std::abs(std::abs(direction.x) - 1) < kEpsilon)
			{
				parametricEquations[1].constant = EndPoint1.position.y;
			}
			else
			{
				DBG_IF(flux::verbose, "opa");
				parametricEquations[1].constant = (direction.y / (double) direction.x) * EndPoint1.position.x - EndPoint1.position.y;
			}

			parametricEquations[0].coeficient = direction.x;
			parametricEquations[1].coeficient = direction.y;
		}

		bool operator==(Line& l)
		{
			if (parametricEquations.size() != l.parametricEquations.size()) return false;

			bool avaliationSameDirection = true;
			bool avaliationOpositeDirection = true;

			for (int i = 0; i < parametricEquations.size(); i++)
			{
				parametricDescription& eq1 = parametricEquations[i];
				parametricDescription& eq2 = l.parametricEquations[i];
				if (std::abs(eq1.coeficient - eq2.coeficient) > kEpsilon || std::abs(eq1.constant - eq2.constant) > kEpsilon)
				{
					avaliationSameDirection = false;
					break;
				}
			}

			for (int i = 0; i < parametricEquations.size(); i++)
			{
				parametricDescription& eq1 = parametricEquations[i];
				parametricDescription& eq2 = l.parametricEquations[i];
				if (std::abs(eq1.coeficient + eq2.coeficient) > kEpsilon || std::abs(eq1.constant - eq2.constant) > kEpsilon)
				{
					avaliationOpositeDirection = false;
					break;
				}
			}

			return avaliationOpositeDirection || avaliationSameDirection;
		}
	};
}
