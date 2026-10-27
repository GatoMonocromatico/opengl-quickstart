// Tests for render/ModelData (GL-free model loading): STL binary/ASCII, glTF, GLB.
// Plain asserts, one binary: build with run_tests.sh or the model_tests CMake target.
#include "render/ModelData.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

static int gPassed = 0, gFailed = 0;

#define CHECK(...) do { \
	if (__VA_ARGS__) gPassed++; \
	else { gFailed++; std::cout << "FAIL line " << __LINE__ << ": " #__VA_ARGS__ "\n"; } \
} while (0)

static bool near(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-5f; }

static std::string tmp(const std::string& name)
{
	const char* dir = std::getenv("TMPDIR");
	return std::string(dir ? dir : "/tmp") + "/qs_model_test_" + name;
}

static void write(const std::string& path, const std::string& bytes)
{
	std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

template <class T> static void put(std::string& s, T v)
{
	char b[sizeof(T)];
	std::memcpy(b, &v, sizeof(T));
	s.append(b, sizeof(T));
}

// A unit right tetrahedron: 4 facets, corners at the origin and on each axis.
static const glm::vec3 kTet[4][3] = {
	{{0, 0, 0}, {0, 1, 0}, {1, 0, 0}},
	{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}},
	{{0, 0, 0}, {0, 0, 1}, {0, 1, 0}},
	{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
};

static void testStlBinary()
{
	std::string s(80, ' ');
	put<uint32_t>(s, 4);
	for (const auto& tri : kTet)
	{
		for (int k = 0; k < 3; k++)
			put<float>(s, 0.0f); // zero normal: the loader computes it
		for (const glm::vec3& v : tri)
		{
			put<float>(s, v.x);
			put<float>(s, v.y);
			put<float>(s, v.z);
		}
		put<uint16_t>(s, 0);
	}
	write(tmp("tet.stl"), s);
	ModelData m;
	std::string err;
	CHECK(loadModelData(tmp("tet.stl"), m, err));
	CHECK(m.parts.size() == 1);
	CHECK(m.parts[0].vertices.size() == 12 && m.parts[0].indices.size() == 12);
	CHECK(near(m.boundsMin, {0, 0, 0}) && near(m.boundsMax, {1, 1, 1}));
	CHECK(near(m.parts[0].vertices[0].normal, {0, 0, -1})); // first facet faces -z
	CHECK(std::abs(glm::length(m.parts[0].vertices[11].normal) - 1.0f) < 1e-5f);
	CHECK(m.parts[0].vertices[0].color.a == 1.0f);
}

static void testStlAscii()
{
	write(tmp("tri.stl"),
	      "solid t\n facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 2 0 0\n   vertex 0 3 0\n"
	      "  endloop\n endfacet\nendsolid t\n");
	ModelData m;
	std::string err;
	CHECK(loadModelData(tmp("tri.stl"), m, err));
	CHECK(m.parts.size() == 1 && m.parts[0].vertices.size() == 3);
	CHECK(near(m.parts[0].vertices[1].position, {2, 0, 0}));
	CHECK(near(m.parts[0].vertices[2].normal, {0, 0, 1}));
	CHECK(near(m.boundsMax, {2, 3, 0}));
}

// One triangle (0,0,0) (1,0,0) (0,1,0), u16 indices, red material, node moved by (1,2,3).
static const char* kBuffer64 = "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA=";

static std::string gltfJson(const std::string& buffer)
{
	return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0,"translation":[1,2,3]}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}}],
"buffers":[)" + buffer + R"(],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
}

static void checkTriangle(const ModelData& m)
{
	CHECK(m.parts.size() == 1);
	if (m.parts.size() != 1)
		return;
	const ModelPart& p = m.parts[0];
	CHECK(p.vertices.size() == 3 && p.indices.size() == 3);
	CHECK(near(p.vertices[0].position, {1, 2, 3}) && near(p.vertices[1].position, {2, 2, 3}));
	CHECK(near(p.vertices[2].position, {1, 3, 3}));
	CHECK(near(p.vertices[0].normal, {0, 0, 1})); // no normals in the file: computed
	CHECK(p.vertices[1].color == glm::vec4(1, 0, 0, 1));
	CHECK(near(m.boundsMin, {1, 2, 3}) && near(m.boundsMax, {2, 3, 3}));
}

static void testGltf()
{
	write(tmp("tri.gltf"), gltfJson(std::string(R"({"byteLength":44,"uri":"data:application/octet-stream;base64,)") + kBuffer64 + "\"}"));
	ModelData m;
	std::string err;
	CHECK(loadModelData(tmp("tri.gltf"), m, err));
	checkTriangle(m);
}

static void testGlb()
{
	// the same triangle as a binary glTF: header, JSON chunk, BIN chunk
	std::string json = gltfJson(R"({"byteLength":44})");
	while (json.size() % 4)
		json += ' ';
	std::string bin;
	const float pos[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
	for (float f : pos)
		put<float>(bin, f);
	for (uint16_t i : {0, 1, 2, 0})
		put<uint16_t>(bin, i);
	std::string glb;
	put<uint32_t>(glb, 0x46546C67); // "glTF"
	put<uint32_t>(glb, 2);
	put<uint32_t>(glb, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
	put<uint32_t>(glb, static_cast<uint32_t>(json.size()));
	put<uint32_t>(glb, 0x4E4F534A); // "JSON"
	glb += json;
	put<uint32_t>(glb, static_cast<uint32_t>(bin.size()));
	put<uint32_t>(glb, 0x004E4942); // "BIN\0"
	glb += bin;
	write(tmp("tri.glb"), glb);
	ModelData m;
	std::string err;
	CHECK(loadModelData(tmp("tri.glb"), m, err));
	checkTriangle(m);
}

static void testErrors()
{
	ModelData m;
	std::string err;
	CHECK(!loadModelData(tmp("missing.glb"), m, err));
	CHECK(err.find("file not found") != std::string::npos);
	CHECK(!loadModelData(tmp("thing.obj"), m, err));
	CHECK(err.find("unsupported model format") != std::string::npos);
	write(tmp("empty.stl"), "solid nothing\nendsolid nothing\n");
	CHECK(!loadModelData(tmp("empty.stl"), m, err));
}

int main()
{
	testStlBinary();
	testStlAscii();
	testGltf();
	testGlb();
	testErrors();
	std::cout << gPassed << " passed, " << gFailed << " failed.\n";
	return gFailed == 0 ? 0 : 1;
}
