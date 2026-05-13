#include "RoundedCorner2D.h"
#include "DebugLog.h"

#include <algorithm>
#include <complex>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
		MDBG(DBG_N("phase", "solveCircleCenterFromTangency begin"), DBG_N("corner.x", DBG_F6(corner.x)), DBG_N("corner.y", DBG_F6(corner.y)), DBG_N("p0.x", DBG_F6(p0.x)), DBG_N("p0.y", DBG_F6(p0.y)), DBG_N("p1.x", DBG_F6(p1.x)), DBG_N("p1.y", DBG_F6(p1.y)));

		const glm::vec2 d0 = safeNormalize2(p0 - corner);
		const glm::vec2 d1 = safeNormalize2(p1 - corner);
		if (glm::length(d0) < kEpsilon || glm::length(d1) < kEpsilon)
		{
			MDBG(DBG_N("phase", "solveCircleCenterFromTangency fail: degenerate direction"), DBG_N("d0_len", DBG_F6(glm::length(d0))), DBG_N("d1_len", DBG_F6(glm::length(d1))));
			return false;
		}

		const float angle = std::acos(glm::clamp(glm::dot(d0, d1), -1.0f, 1.0f));
		if (angle < kEpsilon || std::abs(angle - glm::pi<float>()) < kEpsilon)
		{
			MDBG(DBG_N("phase", "solveCircleCenterFromTangency fail: invalid angle"), DBG_N("angle_rad", DBG_F6(angle)));
			return false;
		}

		const float cutDistance0 = glm::length(p0 - corner);
		const float cutDistance1 = glm::length(p1 - corner);
		if (cutDistance0 < kEpsilon || cutDistance1 < kEpsilon)
		{
			MDBG(DBG_N("phase", "solveCircleCenterFromTangency fail: cut distance too small"), DBG_N("cutDistance0", DBG_F6(cutDistance0)), DBG_N("cutDistance1", DBG_F6(cutDistance1)));
			return false;
		}

		const float cutDistance = std::min(cutDistance0, cutDistance1);
		const float radius = cutDistance * std::tan(angle * 0.5f);
		if (radius < kEpsilon)
		{
			MDBG(DBG_N("phase", "solveCircleCenterFromTangency fail: radius too small"), DBG_N("radius", DBG_F6(radius)), DBG_N("cutDistance", DBG_F6(cutDistance)));
			return false;
		}

		glm::vec2 bisector = safeNormalize2(d0 + d1);
		if (glm::length(bisector) < kEpsilon)
		{
			MDBG(DBG_N("phase", "solveCircleCenterFromTangency fail: bisector too small"), DBG_N("bisector_len", DBG_F6(glm::length(bisector))));
			return false;
		}

		const float centerDistance = radius / std::sin(angle * 0.5f);
		outCenter = corner + bisector * centerDistance;
		outRadius = radius;
		MDBG(DBG_N("phase", "solveCircleCenterFromTangency ok"), DBG_N("center.x", DBG_F6(outCenter.x)), DBG_N("center.y", DBG_F6(outCenter.y)), DBG_N("radius", DBG_F6(outRadius)), DBG_N("angle_rad", DBG_F6(angle)), DBG_N("centerDistance", DBG_F6(centerDistance)));
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
	float roundness,
	int roundLineCount)
{
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D begin"), DBG_N("vertices_count", vertices.size()), DBG_N("indices_count", indices.size()), DBG_N("cornerVertexIndex", cornerVertexIndex), DBG_N("roundness", DBG_F6(roundness)), DBG_N("roundLineCount", roundLineCount));

	if (cornerVertexIndex >= vertices.size())
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D skip: invalid cornerVertexIndex"), DBG_N("cornerVertexIndex", cornerVertexIndex), DBG_N("vertices_count", vertices.size()));
		return false;
	}
	if (!std::isfinite(roundness) || roundness < 0.0f || roundness > 1.0f)
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D skip: invalid roundness"), DBG_N("roundness", DBG_F6(roundness)));
		return false;
	}
	if (roundLineCount < 1)
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D skip: invalid roundLineCount"), DBG_N("roundLineCount", roundLineCount));
		return false;
	}

	CornerTriangles data = collectCornerTriangles(indices, cornerVertexIndex);
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D collected corner triangles"), DBG_N("corner_triangles", data.triangleStarts.size()), DBG_N("neighbor_vertices", data.neighborVertices.size()));
	// Scope guard: this implementation supports the existing "quad corner style"
	// where one corner is used by exactly two triangles.
	if (data.triangleStarts.size() != 2)
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D skip: unsupported topology (triangle count)"), DBG_N("corner_triangles", data.triangleStarts.size()));
		return false;
	}
	if (data.neighborVertices.size() != 3)
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D skip: unsupported topology (neighbor count)"), DBG_N("neighbor_vertices", data.neighborVertices.size()));
		return false;
	}

	std::unordered_map<EdgeKey, int, EdgeKeyHasher> edgeUseCount{};
	for (size_t triStart : data.triangleStarts)
	{
		// Triangle-local indices.
		const GLuint a = indices[triStart];
		const GLuint b = indices[triStart + 1];
		const GLuint c = indices[triStart + 2];
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D corner triangle"), DBG_N("triStart", triStart), DBG_N("a", a), DBG_N("b", b), DBG_N("c", c));
		// Count how many times each undirected edge appears around this corner.
		edgeUseCount[makeKey(a, b)] += 1;
		edgeUseCount[makeKey(b, c)] += 1;
		edgeUseCount[makeKey(c, a)] += 1;
	}

	// Supported topology (guarded above): exactly two triangles share the corner.
	// The union of those two triangles contains exactly 3 other vertices:
	// - Two are the endpoints of the corner edges (each edge appears once).
	// - One is the "diagonal" vertex shared by both triangles (edge appears twice).
	std::vector<GLuint> boundaryNeighbors{};
	// This is the vertex shared by both corner triangles (the "diagonal" of a quad split).
	GLuint sharedNeighbor = std::numeric_limits<GLuint>::max();
	for (GLuint n : data.neighborVertices)
	{
		const int uses = edgeUseCount[makeKey(cornerVertexIndex, n)];
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D neighbor edge uses"), DBG_N("neighbor", n), DBG_N("uses", uses));
		if (uses == 1)
		{
			boundaryNeighbors.push_back(n);
		}
		else if (uses == 2)
		{
			sharedNeighbor = n;
		}
	}
	if (boundaryNeighbors.size() != 2 || sharedNeighbor == std::numeric_limits<GLuint>::max())
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D skip: boundary/shared neighbor"), DBG_N("boundaryNeighbors", boundaryNeighbors.size()), DBG_N("sharedNeighbor", sharedNeighbor));
		return false;
	}

	// Endpoints of the two segments that meet at the corner (the two "lines").
	const GLuint edgeEndpointIndex0 = boundaryNeighbors[0];
	const GLuint edgeEndpointIndex1 = boundaryNeighbors[1];
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D resolved neighbor vertices"), DBG_N("edgeEndpointIndex0", edgeEndpointIndex0), DBG_N("edgeEndpointIndex1", edgeEndpointIndex1), DBG_N("sharedNeighbor", sharedNeighbor));

	// `cornerV` is the original sharp corner vertex (will be removed).
	// `edgeV*` are the vertices at the far ends of the two incident edges.
	const Vertex cornerV = vertices[cornerVertexIndex];
	const Vertex edgeV0 = vertices[edgeEndpointIndex0];
	const Vertex edgeV1 = vertices[edgeEndpointIndex1];
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D corner/edge positions"), DBG_N("corner.x", DBG_F6(cornerV.position.x)), DBG_N("corner.y", DBG_F6(cornerV.position.y)), DBG_N("corner.z", DBG_F6(cornerV.position.z)), DBG_N("edge0.x", DBG_F6(edgeV0.position.x)), DBG_N("edge0.y", DBG_F6(edgeV0.position.y)), DBG_N("edge0.z", DBG_F6(edgeV0.position.z)), DBG_N("edge1.x", DBG_F6(edgeV1.position.x)), DBG_N("edge1.y", DBG_F6(edgeV1.position.y)), DBG_N("edge1.z", DBG_F6(edgeV1.position.z)));

	// This function rounds a *2D* corner, so geometry is computed in the XY plane.
	// The original Z (cornerV.position.z) is preserved in every generated vertex.
	const glm::vec2 cornerXY(cornerV.position.x, cornerV.position.y);
	const glm::vec2 edgeEndpointXY0(edgeV0.position.x, edgeV0.position.y);
	const glm::vec2 edgeEndpointXY1(edgeV1.position.x, edgeV1.position.y);

	// Vectors from the corner pointing along each of the two edges.
	const glm::vec2 cornerToEdge0 = edgeEndpointXY0 - cornerXY;
	const glm::vec2 cornerToEdge1 = edgeEndpointXY1 - cornerXY;
	const float edgeLength0 = glm::length(cornerToEdge0);
	const float edgeLength1 = glm::length(cornerToEdge1);
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D edge lengths"), DBG_N("edgeLength0", DBG_F6(edgeLength0)), DBG_N("edgeLength1", DBG_F6(edgeLength1)));
	if (edgeLength0 < kEpsilon || edgeLength1 < kEpsilon)
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D skip: degenerate edge length"), DBG_N("edgeLength0", DBG_F6(edgeLength0)), DBG_N("edgeLength1", DBG_F6(edgeLength1)));
		return false;
	}

	// How far we move away from the corner along each edge to start/end the arc.
	// We cap by the shorter edge to avoid cutting past an endpoint.
	const float limitingEdgeLength = std::min(edgeLength0, edgeLength1);
	const float shortenDistance = glm::clamp(roundness * limitingEdgeLength, 0.0f, limitingEdgeLength - kEpsilon);
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D shorten distance"), DBG_N("limitingEdgeLength", DBG_F6(limitingEdgeLength)), DBG_N("shortenDistance", DBG_F6(shortenDistance)));
	if (shortenDistance < kEpsilon)
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D no-op: shortenDistance < epsilon"), DBG_N("shortenDistance", DBG_F6(shortenDistance)));
		return true;
	}

	// New endpoints after shortening each edge; the rounded arc connects these points.
	const glm::vec2 cutPointXY0 = cornerXY + safeNormalize2(cornerToEdge0) * shortenDistance;
	const glm::vec2 cutPointXY1 = cornerXY + safeNormalize2(cornerToEdge1) * shortenDistance;
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D cut points"), DBG_N("cut0.x", DBG_F6(cutPointXY0.x)), DBG_N("cut0.y", DBG_F6(cutPointXY0.y)), DBG_N("cut1.x", DBG_F6(cutPointXY1.x)), DBG_N("cut1.y", DBG_F6(cutPointXY1.y)));

	// circleCenter/radius define the fillet arc tangent to both shortened lines.
	glm::vec2 circleCenter(0.0f);
	float radius = 0.0f;
	if (!solveCircleCenterFromTangency(cornerXY, cutPointXY0, cutPointXY1, circleCenter, radius))
	{
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D skip: could not solve circle tangency"));
		return false;
	}
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D circle solved"), DBG_N("center.x", DBG_F6(circleCenter.x)), DBG_N("center.y", DBG_F6(circleCenter.y)), DBG_N("radius", DBG_F6(radius)));

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
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D arc angles"), DBG_N("startAngle", DBG_F6(startAngle)), DBG_N("endAngle", DBG_F6(endAngle)), DBG_N("delta", DBG_F6(delta)), DBG_N("step", DBG_F6(step)));

	// All generated vertices inherit the original corner's attributes (UVs, color, intensity...).
	// Only the position changes.
	Vertex arcStartVertex = cornerV;
	arcStartVertex.position = glm::vec3(cutPointXY0.x, cutPointXY0.y, cornerV.position.z);
	Vertex arcEndVertex = cornerV;
	arcEndVertex.position = glm::vec3(cutPointXY1.x, cutPointXY1.y, cornerV.position.z);

	// Store the arc start vertex and remember its index.
	const GLuint arcStartIndex = static_cast<GLuint>(vertices.size());
	vertices.push_back(arcStartVertex);
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D arc start vertex pushed"), DBG_N("arcStartIndex", arcStartIndex), DBG_N("vertices_count", vertices.size()));

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
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D arc mid vertex pushed"), DBG_N("i", i), DBG_N("idx", arcIndices.back()), DBG_N("pos.x", DBG_F6(arcV.position.x)), DBG_N("pos.y", DBG_F6(arcV.position.y)), DBG_N("pos.z", DBG_F6(arcV.position.z)));
	}

	const GLuint arcEndIndex = static_cast<GLuint>(vertices.size());
	vertices.push_back(arcEndVertex);	
	
	arcIndices.push_back(arcEndIndex);
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D arc end vertex pushed"), DBG_N("arcEndIndex", arcEndIndex), DBG_N("pos.x", DBG_F6(arcEndVertex.position.x)), DBG_N("pos.y", DBG_F6(arcEndVertex.position.y)), DBG_N("pos.z", DBG_F6(arcEndVertex.position.z)), DBG_N("arcVertexCount", arcIndices.size()));

	std::vector<GLuint> rebuiltIndices{};
	rebuiltIndices.reserve(indices.size() + static_cast<size_t>(roundLineCount) * 3);

	// Remove ALL previous corner triangles so old sharp corner is not drawn anymore.
	std::unordered_set<size_t> removeTriStarts(data.triangleStarts.begin(), data.triangleStarts.end());
	for (size_t i = 0; i + 2 < indices.size(); i += 3)
	{
		GLuint i1 = indices[i];
		GLuint i2 = indices[i+1];
		GLuint i3 = indices[i+2];

		if (removeTriStarts.count(i) > 0)
		{
			GLuint thisCutPoint;

			Line startArcLine(arcStartIndex, cornerVertexIndex, vertices);
			Line endArcLine(arcEndIndex, cornerVertexIndex, vertices);
			Line line1(i1, cornerVertexIndex, vertices);
			Line line2(i2, cornerVertexIndex, vertices);
			Line line3(i3, cornerVertexIndex, vertices);

			MDBG(DBG_N("startArc_match_l1", (line1 == startArcLine)), DBG_N("startArc_match_l2", (line2 == startArcLine)), DBG_N("startArc_match_l3", (line3 == startArcLine)));
			MDBG(DBG_N("endArc_match_l1", (line1 == endArcLine)), DBG_N("endArc_match_l2", (line2 == endArcLine)), DBG_N("endArc_match_l3", (line3 == endArcLine)));

			if (line1 == startArcLine || line2 == startArcLine || line3 == startArcLine)
			{
				thisCutPoint = arcStartIndex;
				MDBG("branch", "startArc cut chosen");
			}
			else if (line1 == endArcLine || line2 == endArcLine || line3 == endArcLine)
			{
				thisCutPoint = arcEndIndex;
				MDBG("branch", "endArc cut chosen");
			}
			else continue;

			if (cornerVertexIndex == i1) i1 = thisCutPoint;
			else if (cornerVertexIndex == i2) i2 = thisCutPoint;
			else if (cornerVertexIndex == i3) i3 = thisCutPoint;
			else continue;
		}
		rebuiltIndices.push_back(i1);
		rebuiltIndices.push_back(i2);
		rebuiltIndices.push_back(i3);
	}
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D indices rebuilt"), DBG_N("removed_triangles", data.triangleStarts.size()), DBG_N("old_indices_count", indices.size()), DBG_N("rebuilt_indices_count", rebuiltIndices.size()));


	// Draw geometry matters here
	if (data.triangleStarts.size() == 2)
	{
		// Replace the two removed triangles with a triangle fan anchored at `sharedNeighbor`.
		// This fills exactly the same region, but with the rounded edge (the arc) as boundary.
		for (size_t i = 0; i + 1 < arcIndices.size(); ++i)
		{
			rebuiltIndices.push_back(sharedNeighbor);
			rebuiltIndices.push_back(arcIndices[i]);
			rebuiltIndices.push_back(arcIndices[i + 1]);
		}
		MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D triangle fan appended"), DBG_N("sharedNeighbor", sharedNeighbor), DBG_N("fan_triangles", (arcIndices.size() > 1 ? (arcIndices.size() - 1) : static_cast<size_t>(0))), DBG_N("rebuilt_indices_count", rebuiltIndices.size()));

		// Remove previous corner vertex and reindex.
		std::vector<Vertex> rebuiltVertices{};
		rebuiltVertices.reserve(vertices.size() - 1);
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			if (i == cornerVertexIndex)
			{
				continue;
			}
			rebuiltVertices.push_back(vertices[i]);
		}

		for (GLuint& idx : rebuiltIndices)
		{
			if (idx == cornerVertexIndex)
			{
				MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D fail: rebuilt indices still reference removed corner"), DBG_N("cornerVertexIndex", cornerVertexIndex));
				return false;
			}
			idx = static_cast<GLuint>(indexAfterRemoval(static_cast<int>(idx), static_cast<int>(cornerVertexIndex)));
		}
		vertices = std::move(rebuiltVertices);
		indices = std::move(rebuiltIndices);
	}
	MDBG(DBG_N("phase", "geometry2d::roundMeshCorner2D done"), DBG_N("vertices_count", vertices.size()), DBG_N("indices_count", indices.size()));
	
	for (Vertex& v : vertices)
	{
		MDBG(DBG_N("vertex.x", DBG_F6(v.position.x)), DBG_N("vertex.y", DBG_F6(v.position.y)));
	}

	return true;
}
