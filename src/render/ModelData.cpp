#include "render/ModelData.h"

#include <cgltf/cgltf.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace
{

Vertex makeVertex(glm::vec3 p, glm::vec3 n, glm::vec4 c)
{
	Vertex v{};
	v.position = p;
	v.normal = n;
	v.color = c;
	v.intensity = 1.0f;
	return v;
}

// Area-weighted vertex normals from the triangles, for files without normals.
void smoothNormals(ModelPart& part)
{
	for (Vertex& v : part.vertices)
		v.normal = glm::vec3(0.0f);
	for (size_t i = 0; i + 2 < part.indices.size(); i += 3)
	{
		Vertex& a = part.vertices[part.indices[i]];
		Vertex& b = part.vertices[part.indices[i + 1]];
		Vertex& c = part.vertices[part.indices[i + 2]];
		const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
		a.normal += n;
		b.normal += n;
		c.normal += n;
	}
	for (Vertex& v : part.vertices)
		v.normal = glm::length(v.normal) > 0.0f ? glm::normalize(v.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
}

const glm::vec4 kStlColor(0.72f, 0.72f, 0.75f, 1.0f);

void addStlTriangle(ModelPart& part, glm::vec3 n, const glm::vec3 v[3])
{
	if (glm::length(n) < 1e-6f)
		n = glm::cross(v[1] - v[0], v[2] - v[0]);
	n = glm::length(n) > 0.0f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
	for (int k = 0; k < 3; k++)
	{
		part.indices.push_back(static_cast<GLuint>(part.vertices.size()));
		part.vertices.push_back(makeVertex(v[k], n, kStlColor));
	}
}

bool loadStl(const std::string& path, ModelData& out, std::string& error)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
	{
		error = "cannot open " + path;
		return false;
	}
	const std::string bytes((std::istreambuf_iterator<char>(f)), {});
	ModelPart part;

	uint32_t count = 0;
	if (bytes.size() >= 84)
		std::memcpy(&count, bytes.data() + 80, 4);
	// binary: an 80-byte header, a triangle count, then exactly 50 bytes per triangle
	if (bytes.size() >= 84 && bytes.size() == 84 + 50ull * count)
	{
		for (uint32_t t = 0; t < count; t++)
		{
			float f12[12];
			std::memcpy(f12, bytes.data() + 84 + 50ull * t, sizeof f12);
			const glm::vec3 v[3] = {{f12[3], f12[4], f12[5]}, {f12[6], f12[7], f12[8]}, {f12[9], f12[10], f12[11]}};
			addStlTriangle(part, {f12[0], f12[1], f12[2]}, v);
		}
	}
	else
	{
		std::istringstream in(bytes);
		std::string word;
		glm::vec3 normal(0.0f), v[3];
		int nv = 0;
		while (in >> word)
		{
			if (word == "normal")
				in >> normal.x >> normal.y >> normal.z;
			else if (word == "vertex" && nv < 3)
			{
				in >> v[nv].x >> v[nv].y >> v[nv].z;
				nv++;
			}
			else if (word == "endfacet")
			{
				if (nv == 3)
					addStlTriangle(part, normal, v);
				nv = 0;
				normal = glm::vec3(0.0f);
			}
		}
		if (part.vertices.empty())
		{
			error = path + ": not a binary STL and no ASCII facets found";
			return false;
		}
	}
	out.parts.push_back(std::move(part));
	return true;
}

const char* gltfResult(cgltf_result r)
{
	switch (r)
	{
	case cgltf_result_data_too_short: return "data too short";
	case cgltf_result_unknown_format: return "unknown format";
	case cgltf_result_invalid_json: return "invalid JSON";
	case cgltf_result_invalid_gltf: return "invalid glTF";
	case cgltf_result_file_not_found: return "file not found";
	case cgltf_result_io_error: return "I/O error";
	case cgltf_result_out_of_memory: return "out of memory";
	default: return "error";
	}
}

void addGltfNode(const cgltf_node* node, ModelData& out)
{
	if (node->mesh)
	{
		float m[16];
		cgltf_node_transform_world(node, m);
		const glm::mat4 world = glm::make_mat4(m);
		const glm::mat3 normalMat = glm::inverseTranspose(glm::mat3(world));

		for (cgltf_size p = 0; p < node->mesh->primitives_count; p++)
		{
			const cgltf_primitive& prim = node->mesh->primitives[p];
			if (prim.type != cgltf_primitive_type_triangles)
				continue;
			const cgltf_accessor *pos = nullptr, *nrm = nullptr, *col = nullptr;
			for (cgltf_size a = 0; a < prim.attributes_count; a++)
			{
				const cgltf_attribute& at = prim.attributes[a];
				if (at.type == cgltf_attribute_type_position)
					pos = at.data;
				else if (at.type == cgltf_attribute_type_normal)
					nrm = at.data;
				else if (at.type == cgltf_attribute_type_color && at.index == 0)
					col = at.data;
			}
			if (!pos)
				continue;

			glm::vec4 base(1.0f);
			if (prim.material && prim.material->has_pbr_metallic_roughness)
				base = glm::make_vec4(prim.material->pbr_metallic_roughness.base_color_factor);

			ModelPart part;
			for (cgltf_size i = 0; i < pos->count; i++)
			{
				glm::vec3 p3(0.0f), n3(0.0f);
				glm::vec4 c = base;
				cgltf_accessor_read_float(pos, i, glm::value_ptr(p3), 3);
				if (nrm)
					cgltf_accessor_read_float(nrm, i, glm::value_ptr(n3), 3);
				if (col)
				{
					float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
					cgltf_accessor_read_float(col, i, rgba, cgltf_num_components(col->type));
					c = base * glm::make_vec4(rgba);
				}
				const glm::vec3 n = nrm ? glm::normalize(normalMat * n3) : glm::vec3(0.0f);
				part.vertices.push_back(makeVertex(glm::vec3(world * glm::vec4(p3, 1.0f)), n, c));
			}
			if (prim.indices)
				for (cgltf_size k = 0; k < prim.indices->count; k++)
					part.indices.push_back(static_cast<GLuint>(cgltf_accessor_read_index(prim.indices, k)));
			else
				for (cgltf_size k = 0; k < pos->count; k++)
					part.indices.push_back(static_cast<GLuint>(k));
			if (!nrm)
				smoothNormals(part);
			out.parts.push_back(std::move(part));
		}
	}
	for (cgltf_size c = 0; c < node->children_count; c++)
		addGltfNode(node->children[c], out);
}

bool loadGltf(const std::string& path, ModelData& out, std::string& error)
{
	cgltf_options options{};
	cgltf_data* data = nullptr;
	cgltf_result r = cgltf_parse_file(&options, path.c_str(), &data);
	if (r == cgltf_result_success)
		r = cgltf_load_buffers(&options, data, path.c_str());
	if (r == cgltf_result_success)
		r = cgltf_validate(data);
	if (r != cgltf_result_success)
	{
		error = path + ": " + gltfResult(r);
		cgltf_free(data);
		return false;
	}
	if (const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr))
	{
		for (cgltf_size n = 0; n < scene->nodes_count; n++)
			addGltfNode(scene->nodes[n], out);
	}
	else
	{
		for (cgltf_size n = 0; n < data->nodes_count; n++)
			if (!data->nodes[n].parent)
				addGltfNode(&data->nodes[n], out);
	}
	cgltf_free(data);
	if (out.parts.empty())
	{
		error = path + ": no triangle meshes found";
		return false;
	}
	return true;
}

} // namespace

bool loadModelData(const std::string& path, ModelData& out, std::string& error)
{
	out = ModelData{};
	std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

	bool ok;
	if (ext == ".stl")
		ok = loadStl(path, out, error);
	else if (ext == ".gltf" || ext == ".glb")
		ok = loadGltf(path, out, error);
	else
	{
		error = path + ": unsupported model format (use .gltf, .glb or .stl)";
		return false;
	}
	if (!ok)
		return false;

	glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
	for (const ModelPart& part : out.parts)
		for (const Vertex& v : part.vertices)
		{
			lo = glm::min(lo, v.position);
			hi = glm::max(hi, v.position);
		}
	out.boundsMin = lo;
	out.boundsMax = hi;
	return true;
}
