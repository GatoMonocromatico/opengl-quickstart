#include "render/RoundedCorner2D.h"
#include "util/DebugLog.h"

#include <algorithm>
#include <complex>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <array>

namespace
{
	constexpr float kEpsilon = 1e-5f;

	struct CornerTriangles
	{
		// Index offsets (in indices) for each triangle that still uses the old corner.
		std::vector<size_t> triangleStarts;
		// Unique non-corner vertices attached to those triangles.
		std::unordered_set<GLuint> neighborVertices;
	};

	CornerTriangles collectCornerTriangles(const std::vector<GLuint>& indices, GLuint cornerIndex)
	{
		CornerTriangles out{};
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const GLuint a = indices[i];
			const GLuint b = indices[i + 1];
			const GLuint c = indices[i + 2];
			if (a != cornerIndex && b != cornerIndex && c != cornerIndex)
			{
				continue;
			}

			out.triangleStarts.push_back(i);
			if (a != cornerIndex) out.neighborVertices.insert(a);
			if (b != cornerIndex) out.neighborVertices.insert(b);
			if (c != cornerIndex) out.neighborVertices.insert(c);
		}
		return out;
	}

	glm::vec2 safeNormalize2(const glm::vec2& v)
	{
		const float len = glm::length(v);
		if (len < kEpsilon)
		{
			return glm::vec2(0.0f);
		}
		return v / len;
	}

	struct EdgeKey
	{
		// Undirected edge key (smallest index first) so AB and BA are equivalent.
		GLuint lo;
		GLuint hi;

		bool operator==(const EdgeKey& rhs) const
		{
			return lo == rhs.lo && hi == rhs.hi;
		}
	};

	struct EdgeKeyHasher
	{
		size_t operator()(const EdgeKey& k) const
		{
			// Why hashing here:
			// We need to count how often each *undirected edge* appears in the local
			// set of corner triangles (to infer which neighbor vertex is "shared" and
			// which two vertices define the two corner edges).
			//
			// `unordered_map` gives O(1) average-time inserts/lookups, which matters if
			// you generate/round many corners per frame (e.g. UI meshes, text meshes).
			// `std::map` would add O(log N) overhead with no benefit for this tiny key.
			return (static_cast<size_t>(k.lo) << 32) ^ static_cast<size_t>(k.hi);
		}
	};

	EdgeKey makeKey(GLuint a, GLuint b)
	{
		return (a < b) ? EdgeKey{ a, b } : EdgeKey{ b, a };
	}

	bool solveCircleCenterFromTangency(
		const glm::vec2& corner,
		const glm::vec2& p0,
		const glm::vec2& p1,
		glm::vec2& outCenter,
		float& outRadius)
	{
		DBG_IF(flux::verbose, "solveCircleCenterFromTangency: begin");
		MDBG_IF(flux::verbose, DBG_N("corner.x", DBG_F6(corner.x)), DBG_N("corner.y", DBG_F6(corner.y)), DBG_N("p0.x", DBG_F6(p0.x)), DBG_N("p0.y", DBG_F6(p0.y)), DBG_N("p1.x", DBG_F6(p1.x)), DBG_N("p1.y", DBG_F6(p1.y)));

		const glm::vec2 d0 = safeNormalize2(p0 - corner);
		const glm::vec2 d1 = safeNormalize2(p1 - corner);
		if (glm::length(d0) < kEpsilon || glm::length(d1) < kEpsilon)
		{
			MDBG_IF(flux::verbose, DBG_N("phase", "solveCircleCenterFromTangency fail: degenerate direction"), DBG_N("d0_len", DBG_F6(glm::length(d0))), DBG_N("d1_len", DBG_F6(glm::length(d1))));
			return false;
		}

		const float angle = std::acos(glm::clamp(glm::dot(d0, d1), -1.0f, 1.0f));
		if (angle < kEpsilon || std::abs(angle - glm::pi<float>()) < kEpsilon)
		{
			MDBG_IF(flux::verbose, DBG_N("phase", "solveCircleCenterFromTangency fail: invalid angle"), DBG_N("angle_rad", DBG_F6(angle)));
			return false;
		}

		const float cutDistance0 = glm::length(p0 - corner);
		const float cutDistance1 = glm::length(p1 - corner);
		if (cutDistance0 < kEpsilon || cutDistance1 < kEpsilon)
		{
			MDBG_IF(flux::verbose, DBG_N("phase", "solveCircleCenterFromTangency fail: cut distance too small"), DBG_N("cutDistance0", DBG_F6(cutDistance0)), DBG_N("cutDistance1", DBG_F6(cutDistance1)));
			return false;
		}

		const float cutDistance = std::min(cutDistance0, cutDistance1);
		const float radius = cutDistance * std::tan(angle * 0.5f);
		if (radius < kEpsilon)
		{
			MDBG_IF(flux::verbose, DBG_N("phase", "solveCircleCenterFromTangency fail: radius too small"), DBG_N("radius", DBG_F6(radius)), DBG_N("cutDistance", DBG_F6(cutDistance)));
			return false;
		}

		glm::vec2 bisector = safeNormalize2(d0 + d1);
		if (glm::length(bisector) < kEpsilon)
		{
			MDBG_IF(flux::verbose, DBG_N("phase", "solveCircleCenterFromTangency fail: bisector too small"), DBG_N("bisector_len", DBG_F6(glm::length(bisector))));
			return false;
		}

		const float centerDistance = radius / std::sin(angle * 0.5f);
		outCenter = corner + bisector * centerDistance;
		outRadius = radius;
		DBG_IF(flux::verbose, "solveCircleCenterFromTangency: ok");
		MDBG_IF(flux::verbose, DBG_N("center.x", DBG_F6(outCenter.x)), DBG_N("center.y", DBG_F6(outCenter.y)), DBG_N("radius", DBG_F6(outRadius)), DBG_N("angle_rad", DBG_F6(angle)), DBG_N("centerDistance", DBG_F6(centerDistance)));
		return true;
	}

	int indexAfterRemoval(int idx, int removedIdx)
	{
		return (idx > removedIdx) ? (idx - 1) : idx;
	}
}

bool geometry2d::roundMeshCorner2D(
	std::vector<Vertex>& vertices,
	std::vector<GLuint>& indices,
	GLuint cornerVertexIndex,
	float shortenDistance,
	int roundLineCount)
{
	DBG_IF(flux::verbose, "roundMeshCorner2D: begin");
	MDBG_IF(flux::verbose, DBG_N("vertices_count", vertices.size()), DBG_N("indices_count", indices.size()), DBG_N("cornerVertexIndex", cornerVertexIndex), DBG_N("shortenDistance", DBG_F6(shortenDistance)), DBG_N("roundLineCount", roundLineCount));

	if (cornerVertexIndex >= vertices.size())
	{
		MDBG_IF(flux::verbose, DBG_N("phase", "roundMeshCorner2D skip: invalid cornerVertexIndex"), DBG_N("cornerVertexIndex", cornerVertexIndex), DBG_N("vertices_count", vertices.size()));
		return false;
	}
	if (!std::isfinite(shortenDistance) || shortenDistance < 0.0f)
	{
		MDBG_IF(flux::verbose, DBG_N("phase", "roundMeshCorner2D skip: invalid shortenDistance"), DBG_N("shortenDistance", DBG_F6(shortenDistance)));
		return false;
	}
	if (roundLineCount < 1)
	{
		MDBG_IF(flux::verbose, DBG_N("phase", "roundMeshCorner2D skip: invalid roundLineCount"), DBG_N("roundLineCount", roundLineCount));
		return false;
	}

	CornerTriangles data = collectCornerTriangles(indices, cornerVertexIndex);
	MDBG_IF(flux::verbose, DBG_N("phase", "collected corner triangles"), DBG_N("corner_triangles", data.triangleStarts.size()), DBG_N("neighbor_vertices", data.neighborVertices.size()));
	// Require at least two corner triangles.  N >= 2 is supported for star topology:
	// N boundary edges fan out from the corner and all N triangles share one inner vertex.

	std::unordered_map<EdgeKey, int, EdgeKeyHasher> edgeUseCount{};
	for (size_t triStart : data.triangleStarts)
	{
		// Triangle-local indices.
		const GLuint a = indices[triStart];
		const GLuint b = indices[triStart + 1];
		const GLuint c = indices[triStart + 2];
		MDBG_IF(flux::verbose, DBG_N("phase", "corner triangle"), DBG_N("triStart", triStart), DBG_N("a", a), DBG_N("b", b), DBG_N("c", c));
		// Count how many times each undirected edge appears around this corner.
		edgeUseCount[makeKey(a, b)] += 1;
		edgeUseCount[makeKey(b, c)] += 1;
		edgeUseCount[makeKey(c, a)] += 1;
	}

	// Star topology: N boundary edges fan out from the corner and all N triangles share
	// exactly one inner vertex (sharedNeighbor).  Each boundary neighbor edge appears once;
	// the shared-neighbor edge appears N times (once per corner triangle).
	// Fan topology (multiple vertices with uses > 1) is not supported.
	std::vector<GLuint> boundaryNeighbors{};
	GLuint sharedNeighbor = std::numeric_limits<GLuint>::max();
	for (GLuint n : data.neighborVertices)
	{
		const int uses = edgeUseCount[makeKey(cornerVertexIndex, n)];
		MDBG_IF(flux::verbose, DBG_N("phase", "neighbor edge uses"), DBG_N("neighbor", n), DBG_N("uses", uses));
		if (uses == 1)
		{
			boundaryNeighbors.push_back(n);
		}
		else if (uses > 1 && sharedNeighbor == std::numeric_limits<GLuint>::max())
		{
			sharedNeighbor = n;
		}
	}
	if (sharedNeighbor == std::numeric_limits<GLuint>::max())
	{
		MDBG_IF(flux::verbose, "sharedNeighbor was not determined", DBG_N("boundaryNeighbors", boundaryNeighbors.size()), DBG_N("sharedNeighbor", sharedNeighbor));
		if (boundaryNeighbors.size()  != 2)
		{
			DBG_IF(flux::verbose, "Unsuported topology: boundaryNeighbors.size() != 2");
			return false;
		}
		sharedNeighbor = boundaryNeighbors[0];
		DBG_IF(flux::verbose, "sharedNeighbor", sharedNeighbor);
	}

	// `cornerV` is the original sharp corner vertex (will be removed).
	const Vertex cornerV = vertices[cornerVertexIndex];

	// This function rounds a *2D* corner, so geometry is computed in the XY plane.
	// The original Z (cornerV.position.z) is preserved in every generated vertex.
	const glm::vec2 cornerXY(cornerV.position.x, cornerV.position.y);

	// Among all boundary neighbors, find the pair whose edge directions from the corner
	// span the largest angle.  For N == 2 this is the only pair; for N > 2 this selects
	// the two outermost edges that define the rounding arc.
	GLuint edgeEndpointIndex0 = boundaryNeighbors[0];
	GLuint edgeEndpointIndex1 = boundaryNeighbors[1];
	{
		float maxAngle = -1.0f;
		for (size_t bi = 0; bi < boundaryNeighbors.size(); ++bi)
		{
			const glm::vec2 di = safeNormalize2(
				glm::vec2(vertices[boundaryNeighbors[bi]].position.x,
				          vertices[boundaryNeighbors[bi]].position.y) - cornerXY);
			for (size_t bj = bi + 1; bj < boundaryNeighbors.size(); ++bj)
			{
				const glm::vec2 dj = safeNormalize2(
					glm::vec2(vertices[boundaryNeighbors[bj]].position.x,
					          vertices[boundaryNeighbors[bj]].position.y) - cornerXY);
				const float angle = std::acos(glm::clamp(glm::dot(di, dj), -1.0f, 1.0f));
				if (angle > maxAngle)
				{
					maxAngle = angle;
					edgeEndpointIndex0 = boundaryNeighbors[bi];
					edgeEndpointIndex1 = boundaryNeighbors[bj];
				}
			}
		}
	}
	MDBG_IF(flux::verbose, DBG_N("phase", "resolved neighbor vertices"), DBG_N("edgeEndpointIndex0", edgeEndpointIndex0), DBG_N("edgeEndpointIndex1", edgeEndpointIndex1), DBG_N("sharedNeighbor", sharedNeighbor));

	// `edgeV*` are the vertices at the far ends of the two outermost incident edges.
	const Vertex edgeV0 = vertices[edgeEndpointIndex0];
	const Vertex edgeV1 = vertices[edgeEndpointIndex1];
	MDBG_IF(flux::verbose, DBG_N("phase", "corner/edge positions"), DBG_N("corner.x", DBG_F6(cornerV.position.x)), DBG_N("corner.y", DBG_F6(cornerV.position.y)), DBG_N("corner.z", DBG_F6(cornerV.position.z)), DBG_N("edge0.x", DBG_F6(edgeV0.position.x)), DBG_N("edge0.y", DBG_F6(edgeV0.position.y)), DBG_N("edge0.z", DBG_F6(edgeV0.position.z)), DBG_N("edge1.x", DBG_F6(edgeV1.position.x)), DBG_N("edge1.y", DBG_F6(edgeV1.position.y)), DBG_N("edge1.z", DBG_F6(edgeV1.position.z)));

	const glm::vec2 edgeEndpointXY0(edgeV0.position.x, edgeV0.position.y);
	const glm::vec2 edgeEndpointXY1(edgeV1.position.x, edgeV1.position.y);

	// Vectors from the corner pointing along each of the two edges.
	const glm::vec2 cornerToEdge0 = edgeEndpointXY0 - cornerXY;
	const glm::vec2 cornerToEdge1 = edgeEndpointXY1 - cornerXY;
	const float edgeLength0 = glm::length(cornerToEdge0);
	const float edgeLength1 = glm::length(cornerToEdge1);
	MDBG_IF(flux::verbose, DBG_N("phase", "edge lengths"), DBG_N("edgeLength0", DBG_F6(edgeLength0)), DBG_N("edgeLength1", DBG_F6(edgeLength1)));
	if (edgeLength0 < kEpsilon || edgeLength1 < kEpsilon)
	{
		MDBG_IF(flux::verbose, DBG_N("phase", "roundMeshCorner2D skip: degenerate edge length"), DBG_N("edgeLength0", DBG_F6(edgeLength0)), DBG_N("edgeLength1", DBG_F6(edgeLength1)));
		return false;
	}

	// Cap the requested shorten distance so it never exceeds the shorter edge.
	const float limitingEdgeLength = std::min(edgeLength0, edgeLength1);
	const float clampedShortenDistance = glm::clamp(shortenDistance, 0.0f, limitingEdgeLength - kEpsilon);
	MDBG_IF(flux::verbose, DBG_N("phase", "shorten distance"), DBG_N("limitingEdgeLength", DBG_F6(limitingEdgeLength)), DBG_N("shortenDistance", DBG_F6(clampedShortenDistance)));
	if (clampedShortenDistance < kEpsilon)
	{
		MDBG_IF(flux::verbose, DBG_N("phase", "roundMeshCorner2D no-op: shortenDistance < epsilon"), DBG_N("shortenDistance", DBG_F6(clampedShortenDistance)));
		return true;
	}

	// New endpoints after shortening each edge; the rounded arc connects these points.
	const glm::vec2 cutPointXY0 = cornerXY + safeNormalize2(cornerToEdge0) * clampedShortenDistance;
	const glm::vec2 cutPointXY1 = cornerXY + safeNormalize2(cornerToEdge1) * clampedShortenDistance;
	MDBG_IF(flux::verbose, DBG_N("phase", "cut points"), DBG_N("cut0.x", DBG_F6(cutPointXY0.x)), DBG_N("cut0.y", DBG_F6(cutPointXY0.y)), DBG_N("cut1.x", DBG_F6(cutPointXY1.x)), DBG_N("cut1.y", DBG_F6(cutPointXY1.y)));

	// circleCenter/radius define the fillet arc tangent to both shortened lines.
	glm::vec2 circleCenter(0.0f);
	float radius = 0.0f;
	if (!solveCircleCenterFromTangency(cornerXY, cutPointXY0, cutPointXY1, circleCenter, radius))
	{
		MDBG_IF(flux::verbose, DBG_N("phase", "roundMeshCorner2D skip: could not solve circle tangency"));
		return false;
	}
	MDBG_IF(flux::verbose, DBG_N("phase", "circle solved"), DBG_N("center.x", DBG_F6(circleCenter.x)), DBG_N("center.y", DBG_F6(circleCenter.y)), DBG_N("radius", DBG_F6(radius)));

	// Represent the center->point radius vector as a complex number (x + i*y).
	// Rotations become multiplication by cis(theta) instead of manual sin/cos each step.
	std::complex<float> startRadiusVector(cutPointXY0.x - circleCenter.x, cutPointXY0.y - circleCenter.y);
	std::complex<float> endRadiusVector(cutPointXY1.x - circleCenter.x, cutPointXY1.y - circleCenter.y);

	const float startAngle = std::atan2(startRadiusVector.imag(), startRadiusVector.real());
	const float endAngle = std::atan2(endRadiusVector.imag(), endRadiusVector.real());
	float delta = endAngle - startAngle;
	while (delta <= -glm::pi<float>()) delta += glm::two_pi<float>();
	while (delta > glm::pi<float>()) delta -= glm::two_pi<float>();

	const float step = delta / static_cast<float>(roundLineCount);
	const std::complex<float> rotationStep = std::polar(1.0f, step); // cis(step)
	MDBG_IF(flux::verbose, DBG_N("phase", "arc angles"), DBG_N("startAngle", DBG_F6(startAngle)), DBG_N("endAngle", DBG_F6(endAngle)), DBG_N("delta", DBG_F6(delta)), DBG_N("step", DBG_F6(step)));

	// All generated vertices inherit the original corner's attributes (UVs, color, intensity...).
	// Only the position changes.
	Vertex arcStartVertex = cornerV;
	arcStartVertex.position = glm::vec3(cutPointXY0.x, cutPointXY0.y, cornerV.position.z);
	Vertex arcEndVertex = cornerV;
	arcEndVertex.position = glm::vec3(cutPointXY1.x, cutPointXY1.y, cornerV.position.z);

	// Store the arc start vertex and remember its index.
	const GLuint arcStartIndex = static_cast<GLuint>(vertices.size());
	vertices.push_back(arcStartVertex);
	MDBG_IF(flux::verbose, DBG_N("phase", "arc start vertex pushed"), DBG_N("arcStartIndex", arcStartIndex), DBG_N("vertices_count", vertices.size()));

	// Ordered list of all vertices along the rounded boundary, from start -> ... -> end.
	std::vector<GLuint> arcIndices{};
	arcIndices.reserve(static_cast<size_t>(roundLineCount + 1));
	arcIndices.push_back(arcStartIndex);

	std::complex<float> currentRadiusVector = startRadiusVector;
	for (int i = 1; i < roundLineCount; ++i)
	{
		currentRadiusVector *= rotationStep;
		Vertex arcV = cornerV;
		arcV.position = glm::vec3(
			circleCenter.x + currentRadiusVector.real(),
			circleCenter.y + currentRadiusVector.imag(),
			cornerV.position.z
		);
		arcIndices.push_back(static_cast<GLuint>(vertices.size()));
		vertices.push_back(arcV);
		MDBG_IF(flux::verbose, DBG_N("phase", "arc mid vertex pushed"), DBG_N("i", i), DBG_N("idx", arcIndices.back()), DBG_N("pos.x", DBG_F6(arcV.position.x)), DBG_N("pos.y", DBG_F6(arcV.position.y)), DBG_N("pos.z", DBG_F6(arcV.position.z)));
	}

	const GLuint arcEndIndex = static_cast<GLuint>(vertices.size());
	vertices.push_back(arcEndVertex);

	arcIndices.push_back(arcEndIndex);
	MDBG_IF(flux::verbose, DBG_N("phase", "arc end vertex pushed"), DBG_N("arcEndIndex", arcEndIndex), DBG_N("pos.x", DBG_F6(arcEndVertex.position.x)), DBG_N("pos.y", DBG_F6(arcEndVertex.position.y)), DBG_N("pos.z", DBG_F6(arcEndVertex.position.z)), DBG_N("arcVertexCount", arcIndices.size()));

	std::vector<GLuint> rebuiltIndices{};
	rebuiltIndices.reserve(indices.size() + static_cast<size_t>(roundLineCount) * 3);


	Line startArcLine(arcStartIndex, cornerVertexIndex, vertices);
	Line endArcLine(arcEndIndex, cornerVertexIndex, vertices);

	MDBG_IF(flux::verbose, DBG_N("start.direction.x", startArcLine.direction.x), DBG_N("start.direction.y", startArcLine.direction.y), DBG_N("start.constant.x", startArcLine.parametricEquations[0].constant), DBG_N("start.constant.y", startArcLine.parametricEquations[1].constant), DBG_N("end.direction.x", endArcLine.direction.x), DBG_N("end.direction.y", endArcLine.direction.y), DBG_N("end.constant.x", endArcLine.parametricEquations[0].constant), DBG_N("end.constant.y", endArcLine.parametricEquations[1].constant));


	Line separationLine(sharedNeighbor, cornerVertexIndex, vertices);

	MDBG_IF(flux::verbose, DBG_N("sepLine.dir.x", separationLine.parametricEquations[0].coeficient), DBG_N("sepLine.dir.y", separationLine.parametricEquations[1].coeficient));

	auto getCosBetween = [&](glm::vec2 v1, glm::vec2 v2) {
		return (v1.x * v2.x + v1.y * v2.y) / (double)(glm::length(v1) * glm::length(v2));
	};

	auto getDistanceDirectorVecToSepLine = [&](
    unsigned int	idx,
    const Line&		separationLine)
	{
		const Vertex& myPoint = vertices[idx];
		glm::vec2 pointAtSeparationLine = glm::vec2(
			(float)separationLine.parametricEquations[0].constant,
			(float)separationLine.parametricEquations[1].constant);

		MDBG_IF(flux::verbose, DBG_N("pointAtSeparationLine.x", pointAtSeparationLine.x), DBG_N("pointAtSeparationLine.y", pointAtSeparationLine.y));

		glm::vec2 myPointToPointOnSepLine =
		pointAtSeparationLine - glm::vec2(myPoint.position.x, myPoint.position.y);
		MDBG_IF(flux::verbose, DBG_N("myPointToPointOnSepLine.x", myPointToPointOnSepLine.x), DBG_N("myPointToPointOnSepLine.y", myPointToPointOnSepLine.y));

		glm::vec2 triangleAdjacentVec =
		(float)(glm::length(myPointToPointOnSepLine)
		* getCosBetween(myPointToPointOnSepLine, separationLine.direction))
		* separationLine.direction;

		MDBG_IF(flux::verbose, DBG_N("triangleAdjacentVec.x", triangleAdjacentVec.x), DBG_N("triangleAdjacentVec.y", triangleAdjacentVec.y));

		// not ready yet, needs verification to add or subtract triangleAdjacentVec
		glm::vec2 vectorDistanceToSepLineTest1 = myPointToPointOnSepLine + triangleAdjacentVec;
		glm::vec2 vectorDistanceToSepLineTest2 = myPointToPointOnSepLine - triangleAdjacentVec;

		if (glm::length(vectorDistanceToSepLineTest1) < glm::length(vectorDistanceToSepLineTest2))
		{
			return safeNormalize2(vectorDistanceToSepLineTest1);
		}
		else return safeNormalize2(vectorDistanceToSepLineTest2);
	};

	// ternary operation makes so the reference point is always one that is not from the separation line
	glm::vec2 ArcOneDistanceDirectionToSeparationLine = getDistanceDirectorVecToSepLine(startArcLine == separationLine ? arcEndIndex : arcStartIndex, separationLine);

	MDBG_IF(flux::verbose, DBG_N("phase", "getting one side of separation line distance vector"), DBG_N("vec.dir.x", DBG_F6(ArcOneDistanceDirectionToSeparationLine.x)), DBG_N("vec.dir.y", DBG_F6(ArcOneDistanceDirectionToSeparationLine.y)));
	// Remove ALL previous corner triangles so old sharp corner is not drawn anymore.
	std::unordered_set<size_t> removeTriStarts(data.triangleStarts.begin(), data.triangleStarts.end());
	for (size_t i = 0; i + 2 < indices.size(); i += 3)
	{
		std::array<GLuint, 3> triIndices = {indices[i], indices[i+1], indices[i+2]};

		if (removeTriStarts.count(i) > 0)
		{
			for (int j = 0; j < 3; j++)
			{
				if (triIndices[j] == cornerVertexIndex)	{triIndices[j] = triIndices[0]; triIndices[0] = cornerVertexIndex; continue;}
				if (triIndices[j] == sharedNeighbor)	{triIndices[j] = triIndices[1]; triIndices[1] = sharedNeighbor; continue;}
			}

			glm::vec2 pointDistanceDirection = getDistanceDirectorVecToSepLine(triIndices[2], separationLine);

			MDBG_IF(flux::verbose, DBG_N("triStart", i), DBG_N("v1", triIndices[2]), DBG_N("v1.dir.x", DBG_F6(pointDistanceDirection.x)), DBG_N("v1.dir.y", DBG_F6(pointDistanceDirection.y)));

			if (glm::length((pointDistanceDirection - ArcOneDistanceDirectionToSeparationLine)) < kEpsilon)
			{
				triIndices[0] = startArcLine == separationLine ? arcEndIndex : arcStartIndex;
			}
			else
			{
				triIndices[0] = startArcLine == separationLine ? arcStartIndex : arcEndIndex;
			}
		}
		rebuiltIndices.push_back(triIndices[0]);
		rebuiltIndices.push_back(triIndices[2]);
		rebuiltIndices.push_back(triIndices[1]);
	}
	MDBG_IF(flux::verbose, DBG_N("phase", "indices rebuilt"), DBG_N("removed_triangles", data.triangleStarts.size()), DBG_N("old_indices_count", indices.size()), DBG_N("rebuilt_indices_count", rebuiltIndices.size()));


	// Replace the removed corner triangles with a triangle fan anchored at sharedNeighbor.
	// Works for any N >= 2 star-topology corner: the arc spans from arcIndices[0] to
	// arcIndices[last] and the fan fills the region that the old sharp corner occupied.
	for (size_t i = 0; i + 1 < arcIndices.size(); ++i)
	{
		rebuiltIndices.push_back(sharedNeighbor);
		rebuiltIndices.push_back(arcIndices[i]);
		rebuiltIndices.push_back(arcIndices[i + 1]);
	}
	MDBG_IF(flux::verbose, DBG_N("phase", "triangle fan appended"), DBG_N("sharedNeighbor", sharedNeighbor), DBG_N("fan_triangles", (arcIndices.size() > 1 ? (arcIndices.size() - 1) : static_cast<size_t>(0))), DBG_N("rebuilt_indices_count", rebuiltIndices.size()));

	// Remove the original corner vertex and shift every index above it down by one.
	std::vector<Vertex> rebuiltVertices{};
	rebuiltVertices.reserve(vertices.size() - 1);
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		if (i == cornerVertexIndex) continue;
		rebuiltVertices.push_back(vertices[i]);
	}

	for (GLuint& idx : rebuiltIndices)
	{
		if (idx == cornerVertexIndex)
		{
			MDBG_IF(flux::verbose, DBG_N("phase", "roundMeshCorner2D fail: rebuilt indices still reference removed corner"), DBG_N("cornerVertexIndex", cornerVertexIndex));
			return false;
		}
		idx = static_cast<GLuint>(indexAfterRemoval(static_cast<int>(idx), static_cast<int>(cornerVertexIndex)));
	}
	vertices = std::move(rebuiltVertices);
	indices = std::move(rebuiltIndices);
	MDBG_IF(flux::verbose, DBG_N("phase", "roundMeshCorner2D done"), DBG_N("vertices_count", vertices.size()), DBG_N("indices_count", indices.size()));

	for (Vertex& v : vertices)
	{
		MDBG_IF(flux::verbose, DBG_N("vertex.x", DBG_F6(v.position.x)), DBG_N("vertex.y", DBG_F6(v.position.y)));
	}

	return true;
}
