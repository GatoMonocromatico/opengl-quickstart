// -----------------------------------------------------------------------------
// GpuGL.cpp -- gpu::Device on OpenGL 3.3 core
// -----------------------------------------------------------------------------
// Pass calls run straight away as GL calls. Two things keep SDL_GPU's
// conventions on GL:
//  - render targets: every vertex shader built by qs_gpu_shaders() multiplies
//    gl_Position.y by gpu_flipY; it is -1 while drawing into a texture, so row 0
//    of a rendered texture is its top like an uploaded one (and front faces flip
//    with it). The window keeps +1.
//  - bindings: GLSL 330 has no binding qualifiers, so after linking, uniform
//    blocks and samplers are pointed at their slots by name, from the table the
//    build wrote next to each shader (.refl). Vertex blocks take binding points
//    0-7 and fragment blocks 8-15; fragment samplers units 0-15, vertex 16-23.

#include "gpu/GL.h"
#include "gpu/Gpu.h"
#include "core/Window.h"
#include "render/Framebuffer.h"

#include <glad/glad.h>

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace gpu
{
namespace
{

constexpr int kBlockBase[2] = { 0, 8 };    // vertex, fragment
constexpr int kUnitBase[2] = { 16, 0 };
constexpr int kMaxUnits = 24;

int stageIndex(Stage s) { return s == Stage::Vertex ? 0 : 1; }

struct FormatInfo { GLenum internal, format, type; int bytes; };

FormatInfo formatInfo(Format f)
{
	switch (f)
	{
	case Format::RGBA8: return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4 };
	case Format::R8: return { GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1 };
	case Format::RG8: return { GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2 };
	case Format::RGBA16F: return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8 };
	case Format::Depth24Stencil8: return { GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4 };
	case Format::Depth32F: return { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4 };
	case Format::None: break;
	}
	throw std::runtime_error("gpu: texture format None");
}

GLenum compareFunc(Compare c)
{
	switch (c)
	{
	case Compare::Never: return GL_NEVER;
	case Compare::Less: return GL_LESS;
	case Compare::LessEqual: return GL_LEQUAL;
	case Compare::Equal: return GL_EQUAL;
	case Compare::Greater: return GL_GREATER;
	case Compare::GreaterEqual: return GL_GEQUAL;
	case Compare::NotEqual: return GL_NOTEQUAL;
	case Compare::Always: return GL_ALWAYS;
	}
	return GL_LESS;
}

GLenum primitiveMode(Primitive p)
{
	switch (p)
	{
	case Primitive::Triangles: return GL_TRIANGLES;
	case Primitive::TriangleStrip: return GL_TRIANGLE_STRIP;
	case Primitive::Lines: return GL_LINES;
	case Primitive::LineStrip: return GL_LINE_STRIP;
	case Primitive::Points: return GL_POINTS;
	}
	return GL_TRIANGLES;
}

GLint wrapMode(Wrap w)
{
	switch (w)
	{
	case Wrap::Clamp: return GL_CLAMP_TO_EDGE;
	case Wrap::Repeat: return GL_REPEAT;
	case Wrap::Mirror: return GL_MIRRORED_REPEAT;
	}
	return GL_CLAMP_TO_EDGE;
}

// the coverage blend initialize() sets, which every pass goes back to
void defaultBlend() { glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }

std::string readFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) throw std::runtime_error("gpu: can't read " + path);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

// Slots for handles: id = index + 1, released slots are reused.
template <class T> struct Pool
{
	std::vector<T> items;
	std::vector<uint32_t> free;

	uint32_t add(T t)
	{
		t.live = true;
		if (!free.empty())
		{
			const uint32_t i = free.back();
			free.pop_back();
			items[i] = std::move(t);
			return i + 1;
		}
		items.push_back(std::move(t));
		return static_cast<uint32_t>(items.size());
	}
	T* get(uint32_t id)
	{
		if (id == 0 || id > items.size() || !items[id - 1].live) return nullptr;
		return &items[id - 1];
	}
	const T* get(uint32_t id) const { return const_cast<Pool*>(this)->get(id); }
	void remove(uint32_t id)
	{
		if (T* t = get(id))
		{
			*t = T{};
			free.push_back(id - 1);
		}
	}
};

struct GLBuffer { bool live = false; GLuint name = 0; uint32_t size = 0; };
struct GLTexture { bool live = false; GLuint name = 0; TextureDesc desc; };
struct GLSampler { bool live = false; GLuint name = 0; };
struct Binding { std::string name; int slot; };
struct GLShader
{
	bool live = false;
	GLuint name = 0;
	Stage stage = Stage::Vertex;
	std::vector<Binding> blocks, samplers;
};
struct GLPipeline
{
	bool live = false;
	GLuint program = 0, vao = 0;
	GLint flipLoc = -1;
	PipelineDesc desc;
};

class GLDevice final : public Device
{
public:
	explicit GLDevice(SDL_Window* window) : window_(window) {}

	~GLDevice() override
	{
		for (auto& [key, fbo] : fbos_) glDeleteFramebuffers(1, &fbo);
		for (GLuint ubo : ubos_)
			if (ubo) glDeleteBuffers(1, &ubo);
		for (auto& b : buffers_.items) if (b.live) glDeleteBuffers(1, &b.name);
		for (auto& t : textures_.items) if (t.live) glDeleteTextures(1, &t.name);
		for (auto& s : samplers_.items) if (s.live) glDeleteSamplers(1, &s.name);
		for (auto& s : shaders_.items) if (s.live) glDeleteShader(s.name);
		for (auto& p : pipelines_.items)
			if (p.live) { glDeleteProgram(p.program); glDeleteVertexArrays(1, &p.vao); }
	}

	Backend backend() const override { return Backend::GL; }

	// --- buffers ---------------------------------------------------------------
	Buffer buffer(uint32_t, uint32_t size) override
	{
		GLBuffer b;
		b.size = size;
		glGenBuffers(1, &b.name);
		glBindBuffer(GL_COPY_WRITE_BUFFER, b.name);
		glBufferData(GL_COPY_WRITE_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
		return { buffers_.add(b) };
	}

	void upload(Buffer buffer, const void* data, uint32_t size, uint32_t offset) override
	{
		const GLBuffer* b = buffers_.get(buffer.id);
		if (!b || offset + size > b->size) throw std::runtime_error("gpu: upload past the end of a buffer");
		glBindBuffer(GL_COPY_WRITE_BUFFER, b->name);
		glBufferSubData(GL_COPY_WRITE_BUFFER, offset, size, data);
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
	}

	uint32_t size(Buffer buffer) const override
	{
		const GLBuffer* b = buffers_.get(buffer.id);
		return b ? b->size : 0;
	}

	// --- textures ----------------------------------------------------------------
	Texture texture(const TextureDesc& desc) override
	{
		const FormatInfo f = formatInfo(desc.format);
		GLTexture t;
		t.desc = desc;
		glGenTextures(1, &t.name);
		glBindTexture(GL_TEXTURE_2D, t.name);
		glTexImage2D(GL_TEXTURE_2D, 0, f.internal, desc.width, desc.height, 0, f.format, f.type, nullptr);
		// its own state matches a default Sampler, for raw GL that samples it without one
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		return { textures_.add(t) };
	}

	void upload(Texture texture, const void* pixels, int x, int y, int w, int h) override
	{
		const GLTexture* t = textures_.get(texture.id);
		if (!t) throw std::runtime_error("gpu: upload to a released texture");
		const FormatInfo f = formatInfo(t->desc.format);
		glBindTexture(GL_TEXTURE_2D, t->name);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, f.format, f.type, pixels);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	std::vector<uint8_t> download(Texture texture) override
	{
		const GLTexture* t = textures_.get(texture.id);
		if (!t) return {};
		const FormatInfo f = formatInfo(t->desc.format);
		std::vector<uint8_t> out(static_cast<size_t>(t->desc.width) * t->desc.height * f.bytes);
		glBindTexture(GL_TEXTURE_2D, t->name);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glGetTexImage(GL_TEXTURE_2D, 0, f.format, f.type, out.data());
		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D, 0);
		return out;
	}

	TextureDesc desc(Texture texture) const override
	{
		const GLTexture* t = textures_.get(texture.id);
		return t ? t->desc : TextureDesc{};
	}

	Sampler sampler(const SamplerDesc& d) override
	{
		GLSampler s;
		glGenSamplers(1, &s.name);
		glSamplerParameteri(s.name, GL_TEXTURE_MIN_FILTER, d.minFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri(s.name, GL_TEXTURE_MAG_FILTER, d.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri(s.name, GL_TEXTURE_WRAP_S, wrapMode(d.wrapU));
		glSamplerParameteri(s.name, GL_TEXTURE_WRAP_T, wrapMode(d.wrapV));
		return { samplers_.add(s) };
	}

	// --- shaders and pipelines ----------------------------------------------------
	Shader shader(const char* path, Stage stage) override
	{
		const std::string base = path;
		const std::string source = readFile(base + ".gl");
		GLShader s;
		s.stage = stage;
		std::istringstream refl(readFile(base + ".refl"));
		std::string kind, name;
		int slot;
		while (refl >> kind >> name >> slot)
			(kind == "ubo" ? s.blocks : s.samplers).push_back({ name, slot });

		s.name = glCreateShader(stage == Stage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
		const char* src = source.c_str();
		glShaderSource(s.name, 1, &src, nullptr);
		glCompileShader(s.name);
		GLint ok = 0;
		glGetShaderiv(s.name, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			char log[2048];
			glGetShaderInfoLog(s.name, sizeof log, nullptr, log);
			glDeleteShader(s.name);
			throw std::runtime_error("gpu: " + base + ".gl doesn't compile: " + log);
		}
		return { shaders_.add(std::move(s)) };
	}

	Pipeline pipeline(const PipelineDesc& desc) override
	{
		const GLShader* vs = shaders_.get(desc.vertex.id);
		const GLShader* fs = shaders_.get(desc.fragment.id);
		if (!vs || !fs) throw std::runtime_error("gpu: pipeline without both shaders");
		GLPipeline p;
		p.desc = desc;
		p.program = glCreateProgram();
		glAttachShader(p.program, vs->name);
		glAttachShader(p.program, fs->name);
		glLinkProgram(p.program);
		glDetachShader(p.program, vs->name);
		glDetachShader(p.program, fs->name);
		GLint ok = 0;
		glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			char log[2048];
			glGetProgramInfoLog(p.program, sizeof log, nullptr, log);
			glDeleteProgram(p.program);
			throw std::runtime_error(std::string("gpu: pipeline doesn't link: ") + log);
		}
		glUseProgram(p.program);
		for (const GLShader* s : { vs, fs })
		{
			const int st = stageIndex(s->stage);
			for (const Binding& b : s->blocks)
			{
				const GLuint index = glGetUniformBlockIndex(p.program, b.name.c_str());
				if (index != GL_INVALID_INDEX) glUniformBlockBinding(p.program, index, kBlockBase[st] + b.slot);
			}
			for (const Binding& b : s->samplers)
			{
				const GLint loc = glGetUniformLocation(p.program, b.name.c_str());
				if (loc >= 0) glUniform1i(loc, kUnitBase[st] + b.slot);
			}
		}
		p.flipLoc = glGetUniformLocation(p.program, "gpu_flipY");
		glUseProgram(0);

		glGenVertexArrays(1, &p.vao);
		glBindVertexArray(p.vao);
		for (const VertexAttribute& a : desc.attributes)
		{
			glEnableVertexAttribArray(a.location);
			const bool inst = a.buffer < desc.buffers.size() && desc.buffers[a.buffer].perInstance;
			glVertexAttribDivisor(a.location, inst ? 1 : 0);
		}
		glBindVertexArray(0);
		return { pipelines_.add(std::move(p)) };
	}

	// --- release -------------------------------------------------------------------
	void release(Buffer b) override
	{
		if (GLBuffer* x = buffers_.get(b.id)) glDeleteBuffers(1, &x->name);
		buffers_.remove(b.id);
	}
	void release(Texture t) override
	{
		if (GLTexture* x = textures_.get(t.id))
		{
			for (auto it = fbos_.begin(); it != fbos_.end();)
			{
				if (it->first.first == t.id || it->first.second == t.id)
				{
					glDeleteFramebuffers(1, &it->second);
					it = fbos_.erase(it);
				}
				else ++it;
			}
			glDeleteTextures(1, &x->name);
		}
		textures_.remove(t.id);
	}
	void release(Sampler s) override
	{
		if (GLSampler* x = samplers_.get(s.id)) glDeleteSamplers(1, &x->name);
		samplers_.remove(s.id);
	}
	void release(Shader s) override
	{
		if (GLShader* x = shaders_.get(s.id)) glDeleteShader(x->name);
		shaders_.remove(s.id);
	}
	void release(Pipeline p) override
	{
		if (GLPipeline* x = pipelines_.get(p.id))
		{
			glDeleteProgram(x->program);
			glDeleteVertexArrays(1, &x->vao);
		}
		pipelines_.remove(p.id);
	}

	void present() override { SDL_GL_SwapWindow(window_); }

	// --- natives -------------------------------------------------------------------
	GLuint nativeBuffer(uint32_t id) const { auto* x = buffers_.get(id); return x ? x->name : 0; }
	GLuint nativeTexture(uint32_t id) const { auto* x = textures_.get(id); return x ? x->name : 0; }
	GLuint nativeSampler(uint32_t id) const { auto* x = samplers_.get(id); return x ? x->name : 0; }
	GLuint nativeProgram(uint32_t id) const { auto* x = pipelines_.get(id); return x ? x->program : 0; }

protected:
	// --- passes --------------------------------------------------------------------
	void passBegin(const PassDesc& d, int& w, int& h) override
	{
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedFbo_);
		glGetIntegerv(GL_VIEWPORT, savedViewport_);
		if (const GLTexture* c = textures_.get(d.color.id))
		{
			w = targetW_ = c->desc.width;
			h = targetH_ = c->desc.height;
			flip_ = true;
			glBindFramebuffer(GL_FRAMEBUFFER, fbo(d.color.id, d.depth.id));
		}
		else
		{
			SDL_GetWindowSizeInPixels(window_, &w, &h);
			targetW_ = w;
			targetH_ = h;
			flip_ = false;
			glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer::screenTarget);
		}
		glViewport(0, 0, w, h);
		glDisable(GL_SCISSOR_TEST);
		GLbitfield mask = 0;
		if (d.clear)
		{
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(d.clearColor[0], d.clearColor[1], d.clearColor[2], d.clearColor[3]);
			mask |= GL_COLOR_BUFFER_BIT;
		}
		if (d.clearDepth)
		{
			glDepthMask(GL_TRUE);
			glClearDepth(d.depthValue);
			mask |= GL_DEPTH_BUFFER_BIT;
		}
		if (mask) glClear(mask);
		current_ = nullptr;
	}

	void passBind(Pipeline pipeline) override
	{
		GLPipeline* p = pipelines_.get(pipeline.id);
		current_ = p;
		if (!p) return;
		const PipelineDesc& d = p->desc;
		glUseProgram(p->program);
		if (p->flipLoc >= 0) glUniform1f(p->flipLoc, flip_ ? -1.0f : 1.0f);
		glBindVertexArray(p->vao);

		switch (d.blend)
		{
		case Blend::Off: glDisable(GL_BLEND); break;
		case Blend::Alpha: glEnable(GL_BLEND); defaultBlend(); break;
		case Blend::Premultiplied:
			glEnable(GL_BLEND);
			glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case Blend::Additive:
			glEnable(GL_BLEND);
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
			break;
		}
		glBlendEquation(GL_FUNC_ADD);

		// GL writes depth only with the test on, so a write without a test is an Always test
		if (d.depthTest || d.depthWrite)
		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(d.depthTest ? compareFunc(d.depthCompare) : GL_ALWAYS);
		}
		else glDisable(GL_DEPTH_TEST);
		glDepthMask(d.depthWrite ? GL_TRUE : GL_FALSE);

		if (d.cull == Cull::None) glDisable(GL_CULL_FACE);
		else
		{
			glEnable(GL_CULL_FACE);
			glCullFace(d.cull == Cull::Back ? GL_BACK : GL_FRONT);
		}
		// the flip into a texture mirrors the winding
		const bool ccw = (d.frontFace == FrontFace::CounterClockwise) != flip_;
		glFrontFace(ccw ? GL_CCW : GL_CW);
	}

	void passVertices(uint32_t slot, Buffer buffer, uint32_t offset) override
	{
		const GLBuffer* b = buffers_.get(buffer.id);
		if (!current_ || !b) return;
		const PipelineDesc& d = current_->desc;
		if (slot >= d.buffers.size()) return;
		glBindBuffer(GL_ARRAY_BUFFER, b->name);
		for (const VertexAttribute& a : d.attributes)
		{
			if (a.buffer != slot) continue;
			const void* at = reinterpret_cast<const void*>(static_cast<uintptr_t>(offset + a.offset));
			const GLsizei stride = static_cast<GLsizei>(d.buffers[slot].stride);
			switch (a.format)
			{
			case VertexFormat::Float: glVertexAttribPointer(a.location, 1, GL_FLOAT, GL_FALSE, stride, at); break;
			case VertexFormat::Float2: glVertexAttribPointer(a.location, 2, GL_FLOAT, GL_FALSE, stride, at); break;
			case VertexFormat::Float3: glVertexAttribPointer(a.location, 3, GL_FLOAT, GL_FALSE, stride, at); break;
			case VertexFormat::Float4: glVertexAttribPointer(a.location, 4, GL_FLOAT, GL_FALSE, stride, at); break;
			case VertexFormat::UByte4Norm: glVertexAttribPointer(a.location, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, at); break;
			}
		}
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	void passIndices(Buffer buffer, IndexType type, uint32_t offset) override
	{
		const GLBuffer* b = buffers_.get(buffer.id);
		if (!current_ || !b) return;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->name); // kept by the bound VAO
		indexType_ = type;
		indexOffset_ = offset;
	}

	void passTexture(Stage stage, uint32_t slot, Texture texture, Sampler sampler) override
	{
		const int unit = kUnitBase[stageIndex(stage)] + static_cast<int>(slot);
		if (unit >= kMaxUnits) return;
		const GLTexture* t = textures_.get(texture.id);
		const GLSampler* s = samplers_.get(sampler.id);
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, t ? t->name : 0);
		glBindSampler(unit, s ? s->name : 0);
		glActiveTexture(GL_TEXTURE0);
		usedUnits_ |= 1u << unit;
	}

	void passUniforms(Stage stage, uint32_t slot, const void* data, uint32_t size) override
	{
		const int binding = kBlockBase[stageIndex(stage)] + static_cast<int>(slot);
		if (slot >= 8) return;
		GLuint& ubo = ubos_[binding];
		if (!ubo) glGenBuffers(1, &ubo);
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
		glBufferData(GL_UNIFORM_BUFFER, size, data, GL_STREAM_DRAW);
		glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}

	// top-left pixels -> GL's bottom-left; a flipped texture target already has row 0 on top
	int glY(int y, int h) const { return flip_ ? y : targetH_ - y - h; }

	void passViewport(int x, int y, int w, int h) override { glViewport(x, glY(y, h), w, h); }

	void passScissor(bool on, int x, int y, int w, int h) override
	{
		if (!on) { glDisable(GL_SCISSOR_TEST); return; }
		glEnable(GL_SCISSOR_TEST);
		glScissor(x, glY(y, h), w, h);
	}

	void passDraw(uint32_t count, uint32_t instances, uint32_t first) override
	{
		if (!current_) return;
		glDrawArraysInstanced(primitiveMode(current_->desc.primitive), static_cast<GLint>(first), static_cast<GLsizei>(count),
		                      static_cast<GLsizei>(instances));
	}

	void passDrawIndexed(uint32_t count, uint32_t instances, uint32_t first, int32_t vertexOffset) override
	{
		if (!current_) return;
		const bool u16 = indexType_ == IndexType::U16;
		const uintptr_t at = indexOffset_ + first * (u16 ? 2u : 4u);
		glDrawElementsInstancedBaseVertex(primitiveMode(current_->desc.primitive), static_cast<GLsizei>(count),
		                                  u16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, reinterpret_cast<const void*>(at),
		                                  static_cast<GLsizei>(instances), vertexOffset);
	}

	// GL goes back to what initialize() set, for raw GL after the pass
	void passEnd() override
	{
		glBindVertexArray(0);
		glUseProgram(0);
		for (int unit = 0; unit < kMaxUnits; unit++)
		{
			if (!(usedUnits_ & (1u << unit))) continue;
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(GL_TEXTURE_2D, 0);
			glBindSampler(unit, 0);
		}
		usedUnits_ = 0;
		glActiveTexture(GL_TEXTURE0);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_CULL_FACE);
		glFrontFace(GL_CCW);
		glEnable(GL_BLEND);
		defaultBlend();
		glBlendEquation(GL_FUNC_ADD);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(savedFbo_));
		glViewport(savedViewport_[0], savedViewport_[1], savedViewport_[2], savedViewport_[3]);
		current_ = nullptr;
	}

private:
	GLuint fbo(uint32_t color, uint32_t depth)
	{
		const auto key = std::make_pair(color, depth);
		if (auto it = fbos_.find(key); it != fbos_.end()) return it->second;
		GLuint f = 0;
		glGenFramebuffers(1, &f);
		glBindFramebuffer(GL_FRAMEBUFFER, f);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures_.get(color)->name, 0);
		if (const GLTexture* d = textures_.get(depth))
		{
			const GLenum attach = d->desc.format == Format::Depth24Stencil8 ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
			glFramebufferTexture2D(GL_FRAMEBUFFER, attach, GL_TEXTURE_2D, d->name, 0);
		}
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			glDeleteFramebuffers(1, &f);
			glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(savedFbo_));
			throw std::runtime_error("gpu: incomplete render target");
		}
		fbos_[key] = f;
		return f;
	}

	SDL_Window* window_;
	Pool<GLBuffer> buffers_;
	Pool<GLTexture> textures_;
	Pool<GLSampler> samplers_;
	Pool<GLShader> shaders_;
	Pool<GLPipeline> pipelines_;
	std::map<std::pair<uint32_t, uint32_t>, GLuint> fbos_;
	GLuint ubos_[16] = {};

	GLPipeline* current_ = nullptr;
	bool flip_ = false;
	int targetW_ = 0, targetH_ = 0;
	GLint savedFbo_ = 0;
	GLint savedViewport_[4] = {};
	IndexType indexType_ = IndexType::U32;
	uint32_t indexOffset_ = 0;
	uint32_t usedUnits_ = 0;
};

const GLDevice* asGL(const Device& d) { return dynamic_cast<const GLDevice*>(&d); }

}

std::unique_ptr<Device> createDevice(SDLState& state, Backend)
{
	return std::make_unique<GLDevice>(state.window);
}

namespace gl
{
unsigned native(const Device& d, Buffer b) { auto* g = asGL(d); return g ? g->nativeBuffer(b.id) : 0; }
unsigned native(const Device& d, Texture t) { auto* g = asGL(d); return g ? g->nativeTexture(t.id) : 0; }
unsigned native(const Device& d, Sampler s) { auto* g = asGL(d); return g ? g->nativeSampler(s.id) : 0; }
unsigned program(const Device& d, Pipeline p) { auto* g = asGL(d); return g ? g->nativeProgram(p.id) : 0; }
}

}
