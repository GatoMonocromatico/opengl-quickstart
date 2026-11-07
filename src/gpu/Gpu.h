#pragma once
// -----------------------------------------------------------------------------
// Gpu.h -- the renderer interface: one API over the GL backend (now) and SDL_GPU
// (Vulkan, later)
// -----------------------------------------------------------------------------
// Shaped like SDL_GPU: resources are handles a Device creates and releases;
// drawing state is a Pipeline fixed at creation (shaders, vertex layout, blend,
// depth, cull); uniforms are blocks pushed per stage and slot; a Pass draws into
// one target.
//
//   auto dev = gpu::createDevice(state);
//   gpu::Shader vs = dev->shader(QS_GPU_SHADER_DIR "flat.vert", gpu::Stage::Vertex);
//   gpu::Shader fs = dev->shader(QS_GPU_SHADER_DIR "flat.frag", gpu::Stage::Fragment);
//   gpu::Pipeline p = dev->pipeline({ .vertex = vs, .fragment = fs,
//       .buffers = { { 2 * sizeof(float) } }, .attributes = { { 0, 0, gpu::VertexFormat::Float2, 0 } },
//       .blend = gpu::Blend::Alpha });
//   gpu::Buffer vb = dev->buffer(gpu::BufferUsage::Vertex, sizeof verts);
//   dev->upload(vb, verts, sizeof verts);
//
//   gpu::Pass pass = dev->beginPass({ .clear = true, .clearColor = { 0, 0, 0, 1 } }); // the window
//   pass.bind(p);
//   pass.vertices(0, vb);
//   pass.uniforms(gpu::Stage::Fragment, 0, Tint{ { 1, 0, 0, 1 } });
//   pass.draw(3);
//   pass.end();
//   dev->present();
//
// Conventions (SDL_GPU's, kept by every backend): normalized device coordinates
// have +y up and depth 0 (near) to 1 (far); textures and render targets have their
// origin top-left, so texture row 0 is the top of the image whether it was
// uploaded or rendered, and viewports and scissors are in pixels from the
// target's top-left corner.
//
// Shaders are GLSL 450, compiled at build time by qs_gpu_shaders() (CMake):
// glslang makes SPIR-V, spirv-cross makes the GL backend's GLSL 330 and its
// binding table. They use SDL_GPU's resource layout:
//   vertex:   samplers  layout(set = 0, binding = n)   uniform blocks  layout(set = 1, binding = n)
//   fragment: samplers  layout(set = 2, binding = n)   uniform blocks  layout(set = 3, binding = n)
// Samplers are combined (sampler2D); a uniform block's binding is its slot in
// Pass::uniforms, a sampler's its slot in Pass::texture. Blocks are std140, so a
// C++ struct pushed into one pads vec3s and arrays the way std140 does. On GL,
// varyings are linked by name and blocks and samplers bound by name, so a
// varying keeps its name in both stages and a block or sampler name is used by
// one stage only.
//
// The backend's own objects stay reachable: see gpu/GL.h for GL names, so code
// that wants raw GL can mix it in between passes.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct SDLState;

namespace gpu
{

enum class Backend { GL };

enum class Stage { Vertex, Fragment };

enum class Format { None, RGBA8, R8, RG8, RGBA16F, Depth24Stencil8, Depth32F };

enum class VertexFormat { Float, Float2, Float3, Float4, UByte4Norm };

enum class Primitive { Triangles, TriangleStrip, Lines, LineStrip, Points };

enum class Cull { None, Back, Front };
enum class FrontFace { CounterClockwise, Clockwise };

enum class Compare { Never, Less, LessEqual, Equal, Greater, GreaterEqual, NotEqual, Always };

// Off: replace. Alpha: colour blends over with its alpha, alpha adds up as coverage
// (SRC_ALPHA, ONE_MINUS_SRC_ALPHA for colour; ONE, ONE_MINUS_SRC_ALPHA for alpha), so
// a see-through target ends up right. Premultiplied: the shader's colour is already
// times alpha. Additive: adds colour, keeps the target's alpha.
enum class Blend { Off, Alpha, Premultiplied, Additive };

enum class Filter { Nearest, Linear };
enum class Wrap { Clamp, Repeat, Mirror };

enum class IndexType { U16, U32 };

namespace BufferUsage
{
	enum : uint32_t { Vertex = 1, Index = 2 };
}

namespace TextureUsage
{
	enum : uint32_t { Sampled = 1, ColorTarget = 2, DepthTarget = 4 };
}

// Handles: 0 is none. They copy freely; the Device that made them releases them.
struct Buffer { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct Texture { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct Sampler { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct Shader { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct Pipeline { uint32_t id = 0; explicit operator bool() const { return id != 0; } };

struct TextureDesc
{
	int width = 0, height = 0;
	Format format = Format::RGBA8;
	uint32_t usage = TextureUsage::Sampled;
};

struct SamplerDesc
{
	Filter minFilter = Filter::Linear, magFilter = Filter::Linear;
	Wrap wrapU = Wrap::Clamp, wrapV = Wrap::Clamp;
};

struct VertexBufferLayout
{
	uint32_t stride = 0;
	bool perInstance = false;
};

struct VertexAttribute
{
	uint32_t location = 0;  // the shader's layout(location = n)
	uint32_t buffer = 0;    // index into PipelineDesc::buffers, the slot in Pass::vertices
	VertexFormat format = VertexFormat::Float4;
	uint32_t offset = 0;    // bytes into the vertex
};

struct PipelineDesc
{
	Shader vertex, fragment;
	std::vector<VertexBufferLayout> buffers;
	std::vector<VertexAttribute> attributes;
	Primitive primitive = Primitive::Triangles;
	Cull cull = Cull::None;
	FrontFace frontFace = FrontFace::CounterClockwise;
	Blend blend = Blend::Off;
	bool depthTest = false;
	bool depthWrite = false;
	Compare depthCompare = Compare::Less;
	// What the pipeline draws into. SDL_GPU needs them up front; GL ignores them.
	Format colorFormat = Format::RGBA8;
	Format depthFormat = Format::None;
};

struct PassDesc
{
	// none = the window (on GL: Framebuffer::screenTarget, so a hidden window's app
	// still lands in its offscreen target)
	Texture color;
	bool clear = false;
	float clearColor[4] = { 0, 0, 0, 0 };
	// A DepthTarget texture. None on the window target uses the window's own depth
	// buffer on GL; SDL_GPU will need one given here.
	Texture depth;
	bool clearDepth = false;
	float depthValue = 1.0f;
};

class Device;

// Drawing into one target. Made by Device::beginPass; end() before the next pass,
// before an upload, and before raw backend calls.
class Pass
{
public:
	Pass() = default;

	void bind(Pipeline pipeline);
	void vertices(uint32_t slot, Buffer buffer, uint32_t offset = 0);
	void indices(Buffer buffer, IndexType type = IndexType::U32, uint32_t offset = 0);
	void texture(Stage stage, uint32_t slot, Texture texture, Sampler sampler);
	// A uniform block's contents (std140 layout). It stays set for later draws in
	// this pass until pushed again.
	void uniforms(Stage stage, uint32_t slot, const void* data, uint32_t size);
	template <class T> void uniforms(Stage stage, uint32_t slot, const T& block) { uniforms(stage, slot, &block, sizeof(T)); }
	// pixels from the target's top-left; the whole target by default
	void viewport(int x, int y, int w, int h);
	void scissor(int x, int y, int w, int h);
	void noScissor();
	void draw(uint32_t vertexCount, uint32_t instances = 1, uint32_t firstVertex = 0);
	void drawIndexed(uint32_t indexCount, uint32_t instances = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0);
	void end();

	int width() const { return w_; }
	int height() const { return h_; }
	bool active() const { return dev_ != nullptr; }

private:
	friend class Device;
	Device* dev_ = nullptr;
	int w_ = 0, h_ = 0;
};

class Device
{
public:
	virtual ~Device() = default;
	virtual Backend backend() const = 0;

	virtual Buffer buffer(uint32_t usage, uint32_t size) = 0;
	// bytes into the buffer at offset; outside a pass
	virtual void upload(Buffer buffer, const void* data, uint32_t size, uint32_t offset = 0) = 0;
	virtual uint32_t size(Buffer buffer) const = 0;

	virtual Texture texture(const TextureDesc& desc) = 0;
	// Tightly packed rows of the texture's format, top row first, into the rect at
	// (x, y) from its top-left; outside a pass.
	virtual void upload(Texture texture, const void* pixels, int x, int y, int w, int h) = 0;
	// The whole texture back, top row first (tests, screenshots). Slow: it waits for the GPU.
	virtual std::vector<uint8_t> download(Texture texture) = 0;
	virtual TextureDesc desc(Texture texture) const = 0;

	virtual Sampler sampler(const SamplerDesc& desc) = 0;

	// A shader built by qs_gpu_shaders(): path is its output name without the
	// backend's suffix (QS_GPU_SHADER_DIR "ui2d.vert"). Throws when it can't be
	// loaded or compiled.
	virtual Shader shader(const char* path, Stage stage) = 0;
	// Throws when the shaders don't link.
	virtual Pipeline pipeline(const PipelineDesc& desc) = 0;

	virtual void release(Buffer) = 0;
	virtual void release(Texture) = 0;
	virtual void release(Sampler) = 0;
	virtual void release(Shader) = 0;
	virtual void release(Pipeline) = 0;

	Pass beginPass(const PassDesc& desc);
	// shows the window's frame (GL: SDL_GL_SwapWindow)
	virtual void present() = 0;

protected:
	friend class Pass;
	// begins the pass; returns the target's size
	virtual void passBegin(const PassDesc& desc, int& w, int& h) = 0;
	virtual void passBind(Pipeline) = 0;
	virtual void passVertices(uint32_t slot, Buffer, uint32_t offset) = 0;
	virtual void passIndices(Buffer, IndexType, uint32_t offset) = 0;
	virtual void passTexture(Stage, uint32_t slot, Texture, Sampler) = 0;
	virtual void passUniforms(Stage, uint32_t slot, const void* data, uint32_t size) = 0;
	virtual void passViewport(int x, int y, int w, int h) = 0;
	virtual void passScissor(bool on, int x, int y, int w, int h) = 0;
	virtual void passDraw(uint32_t count, uint32_t instances, uint32_t first) = 0;
	virtual void passDrawIndexed(uint32_t count, uint32_t instances, uint32_t first, int32_t vertexOffset) = 0;
	virtual void passEnd() = 0;
};

// The window's context must be current (initialize() makes it so).
std::unique_ptr<Device> createDevice(SDLState& state, Backend backend = Backend::GL);

inline Pass Device::beginPass(const PassDesc& desc)
{
	Pass p;
	p.dev_ = this;
	passBegin(desc, p.w_, p.h_);
	return p;
}

inline void Pass::bind(Pipeline pipeline) { dev_->passBind(pipeline); }
inline void Pass::vertices(uint32_t slot, Buffer buffer, uint32_t offset) { dev_->passVertices(slot, buffer, offset); }
inline void Pass::indices(Buffer buffer, IndexType type, uint32_t offset) { dev_->passIndices(buffer, type, offset); }
inline void Pass::texture(Stage stage, uint32_t slot, Texture texture, Sampler sampler) { dev_->passTexture(stage, slot, texture, sampler); }
inline void Pass::uniforms(Stage stage, uint32_t slot, const void* data, uint32_t size) { dev_->passUniforms(stage, slot, data, size); }
inline void Pass::viewport(int x, int y, int w, int h) { dev_->passViewport(x, y, w, h); }
inline void Pass::scissor(int x, int y, int w, int h) { dev_->passScissor(true, x, y, w, h); }
inline void Pass::noScissor() { dev_->passScissor(false, 0, 0, 0, 0); }
inline void Pass::draw(uint32_t count, uint32_t instances, uint32_t first) { dev_->passDraw(count, instances, first); }
inline void Pass::drawIndexed(uint32_t count, uint32_t instances, uint32_t first, int32_t vertexOffset) { dev_->passDrawIndexed(count, instances, first, vertexOffset); }
inline void Pass::end()
{
	if (!dev_) return;
	dev_->passEnd();
	dev_ = nullptr;
}

}
