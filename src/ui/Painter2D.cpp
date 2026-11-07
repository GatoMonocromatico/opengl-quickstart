#include "ui/Painter2D.h"
#include "render/Text.h"
#include "render/ShapedText.h"
#include "render/shaderClass.h"
#include "ui/ImageAtlas.h"
#include "gpu/GL.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>

namespace ui
{

Painter2D::Painter2D() = default;
Painter2D::~Painter2D() = default;

// floats per vertex: pos(2) color(4) local(2) shape(4) uvRect(4) mode(1) param0(4) param1(4)
static constexpr int kFloats = 25;
static constexpr int kSizes[8] = { 2, 4, 2, 4, 4, 1, 4, 4 };

// the vertex stage's uniform block and the stock fragment shader's
struct Ui2dView { float screen[2]; float pad[2]; };
struct Ui2dFrag { float dither[2]; float pad[2]; };

constexpr uint32_t kFirstRing = 256 * 1024;

gpu::PipelineDesc Painter2D::pipelineDesc() const
{
	gpu::PipelineDesc d{ .vertex = vs_, .fragment = fs_, .buffers = { { kFloats * sizeof(float) } } };
	uint32_t offset = 0;
	const gpu::VertexFormat formats[5] = { gpu::VertexFormat::Float, gpu::VertexFormat::Float, gpu::VertexFormat::Float2,
	                                       gpu::VertexFormat::Float3, gpu::VertexFormat::Float4 };
	for (uint32_t i = 0; i < 8; i++)
	{
		d.attributes.push_back({ i, 0, formats[kSizes[i]], offset });
		offset += kSizes[i] * sizeof(float);
	}
	// colour blends over; alpha adds up as coverage, so a transparent target ends up right too
	d.blend = gpu::Blend::Alpha;
	return d;
}

void Painter2D::create() { create(gpu::gl::contextDevice()); }

void Painter2D::create(gpu::Device& device)
{
	Delete();
	dev_ = &device;
	vs_ = dev_->shader(QS_GPU_SHADER_DIR "ui2d.vert", gpu::Stage::Vertex);
	fs_ = dev_->shader(QS_GPU_SHADER_DIR "ui2d.frag", gpu::Stage::Fragment);
	Program stock;
	stock.pipeline = dev_->pipeline(pipelineDesc());
	programs_.push_back(std::move(stock));
	vb_ = dev_->buffer(gpu::BufferUsage::Vertex, kFirstRing);
	vbOffset_ = 0;
}

void Painter2D::Delete()
{
	if (dev_)
	{
		for (Program& p : programs_)
		{
			dev_->release(p.pipeline);
			if (p.shader) p.shader->Delete();
		}
		for (auto& [name, t] : wrapped_) dev_->release(t);
		dev_->release(vb_);
		dev_->release(vs_);
		dev_->release(fs_);
	}
	programs_.clear();
	wrapped_.clear();
	vb_ = {};
	vs_ = fs_ = {};
	dev_ = nullptr;
	verts_.clear();
}

gpu::Texture Painter2D::wrapped(unsigned texture)
{
	auto it = wrapped_.find(texture);
	if (it != wrapped_.end()) return it->second;
	const gpu::Texture t = gpu::gl::wrap(*dev_, texture);
	wrapped_[texture] = t;
	return t;
}

void Painter2D::begin(int w, int h)
{
	w_ = w;
	h_ = h;
	verts_.clear();
	tex_ = {};
	clips_.clear();
	xforms_.assign(1, Transform2D{});
	alpha_.assign(1, 1.0f);
	dither_.clear();
	programStack_.clear();
	p0_ = p1_ = glm::vec4(0.0f);
	depthWasOn_ = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
}

void Painter2D::end()
{
	flush();
	clips_.clear();
	glDisable(GL_SCISSOR_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (depthWasOn_) glEnable(GL_DEPTH_TEST);
}

void Painter2D::flush()
{
	if (verts_.empty() || programs_.empty()) { verts_.clear(); return; }
	const Program& p = programs_[programStack_.empty() ? 0 : programStack_.back()];
	const uint32_t bytes = static_cast<uint32_t>(verts_.size() * sizeof(float));
	// the batches of a frame go one after another; when the ring is full it starts
	// over (long-drawn batches by then), and a batch bigger than all of it grows it
	if (vbOffset_ + bytes > dev_->size(vb_))
	{
		vbOffset_ = 0;
		if (bytes > dev_->size(vb_))
		{
			uint32_t size = dev_->size(vb_);
			while (size < bytes) size *= 2;
			dev_->release(vb_);
			vb_ = dev_->buffer(gpu::BufferUsage::Vertex, size);
		}
	}
	dev_->upload(vb_, verts_.data(), bytes, vbOffset_);

	gpu::Pass pass = gpu::gl::beginPassOnBound(*dev_, w_, h_);
	pass.bind(p.pipeline);
	pass.vertices(0, vb_, vbOffset_);
	pass.uniforms(gpu::Stage::Vertex, 0, Ui2dView{ { static_cast<float>(w_), static_cast<float>(h_) }, {} });
	const glm::vec2 d = dither_.empty() ? glm::vec2(1.0f) : dither_.back();
	if (p.shader) glUniform2f(p.ditherLoc, d.x, d.y); // an app's GLSL 330 program: a plain uniform
	else pass.uniforms(gpu::Stage::Fragment, 0, Ui2dFrag{ { d.x, d.y }, {} });
	// no Sampler: each texture samples with its own state, as it always did
	if (tex_) pass.texture(gpu::Stage::Fragment, 0, tex_, {});
	for (size_t i = 0; i < p.samplers.size(); i++)
		pass.texture(gpu::Stage::Fragment, static_cast<uint32_t>(i + 1), wrapped(p.samplers[i].texture), {});
	if (!clips_.empty())
	{
		const ClipRect& c = clips_.back();
		pass.scissor(c.x, c.y, c.w, c.h);
	}
	pass.draw(static_cast<uint32_t>(verts_.size() / kFloats));
	pass.end();
	// between batches GL stays as begin() set it, for raw GL drawn in between
	glDisable(GL_DEPTH_TEST);
	applyClip();

	vbOffset_ += bytes;
	verts_.clear();
	tex_ = {};
}

void Painter2D::useTexture(gpu::Texture texture)
{
	if (texture.id != tex_.id && tex_) flush();
	tex_ = texture;
}

// One quad centered at (cx, cy) with half size (hx, hy), rotated so its x axis
// is (ux, uy). The quad is grown by pad pixels for the antialiasing fringe.
void Painter2D::emit(float cx, float cy, float hx, float hy, float ux, float uy, float radius, float border, const glm::vec4& color,
                     float pad, float mode, const glm::vec4& uv)
{
	const float ex = hx + pad, ey = hy + pad;
	const float nx = -uy, ny = ux;
	const float lx[4] = { -ex, ex, ex, -ex };
	const float ly[4] = { -ey, -ey, ey, ey };
	const int order[6] = { 0, 1, 2, 0, 2, 3 };
	const float a = color.a * alpha_.back();
	for (int k : order)
	{
		const float x = cx + ux * lx[k] + nx * ly[k];
		const float y = cy + uy * lx[k] + ny * ly[k];
		const float v[kFloats] = { x, y, color.r, color.g, color.b, a, lx[k], ly[k], hx, hy, radius, border,
		                           uv.x, uv.y, uv.z, uv.w, mode,
		                           p0_.x, p0_.y, p0_.z, p0_.w, p1_.x, p1_.y, p1_.z, p1_.w };
		verts_.insert(verts_.end(), v, v + kFloats);
	}
}

void Painter2D::quad(float cx, float cy, float hx, float hy, float ux, float uy, float radius, float border, const glm::vec4& color,
                     float mode, const glm::vec4& uv)
{
	const Transform2D& t = transform();
	const glm::vec2 c = t.apply(glm::vec2(cx, cy));
	emit(c.x, c.y, hx * t.scale, hy * t.scale, ux, uy, radius * t.scale, border * t.scale, color, 1.0f, mode, uv);
}

void Painter2D::rect(float x, float y, float w, float h, const glm::vec4& color)
{
	if (w <= 0 || h <= 0) return;
	quad(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, 1, 0, 0, 0, color);
}

void Painter2D::roundedRect(float x, float y, float w, float h, float radius, const glm::vec4& color)
{
	if (w <= 0 || h <= 0) return;
	radius = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
	quad(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, 1, 0, radius, 0, color);
}

void Painter2D::outline(float x, float y, float w, float h, float thickness, const glm::vec4& color, float radius)
{
	if (w <= 0 || h <= 0 || thickness <= 0) return;
	radius = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
	quad(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, 1, 0, radius, thickness, color);
}

void Painter2D::line(float x0, float y0, float x1, float y1, float thickness, const glm::vec4& color)
{
	if (thickness <= 0) return;
	const float dx = x1 - x0, dy = y1 - y0;
	const float len = std::sqrt(dx * dx + dy * dy);
	const float ux = len > 1e-6f ? dx / len : 1.0f;
	const float uy = len > 1e-6f ? dy / len : 0.0f;
	const float r = thickness * 0.5f;
	quad((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, len * 0.5f + r, r, ux, uy, r, 0, color);
}

void Painter2D::circle(float cx, float cy, float radius, const glm::vec4& color)
{
	if (radius <= 0) return;
	quad(cx, cy, radius, radius, 1, 0, radius, 0, color);
}

void Painter2D::circleOutline(float cx, float cy, float radius, float thickness, const glm::vec4& color)
{
	if (radius <= 0 || thickness <= 0) return;
	quad(cx, cy, radius, radius, 1, 0, radius, thickness, color);
}

void Painter2D::image(unsigned texture, float x, float y, float w, float h, const glm::vec4& tint, const glm::vec4& uv)
{
	if (texture) image(wrapped(texture), x, y, w, h, tint, uv);
}

void Painter2D::image(gpu::Texture texture, float x, float y, float w, float h, const glm::vec4& tint, const glm::vec4& uv)
{
	if (w <= 0 || h <= 0 || !texture) return;
	useTexture(texture);
	quad(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, 1, 0, 0, 0, tint, 1.0f, uv);
}

void Painter2D::atlasQuad(ImageAtlas& atlas, int id, float x, float y, float w, float h, const glm::vec4& color, float mode)
{
	if (w <= 0 || h <= 0 || !atlas.valid(id)) return;
	// what's pending from this atlas draws before it grows or changes under it
	if (tex_ && tex_.id == atlas.current().id && atlas.dirty()) flush();
	useTexture(atlas.texture(*dev_));
	const Transform2D& t = transform();
	const glm::vec2 p = glm::round(t.apply(glm::vec2(x, y))); // whole pixels, like glyphs
	const float hx = w * 0.5f * t.scale, hy = h * 0.5f * t.scale;
	emit(p.x + hx, p.y + hy, hx, hy, 1, 0, 0, 0, color, 0.0f, mode, atlas.uv(id));
}

void Painter2D::image(ImageAtlas& atlas, int id, float x, float y, float w, float h, float alpha)
{
	atlasQuad(atlas, id, x, y, w, h, glm::vec4(1.0f, 1.0f, 1.0f, alpha), 4.0f); // premultiplied colours
}

void Painter2D::mask(ImageAtlas& atlas, int id, float x, float y, float w, float h, const glm::vec4& color)
{
	atlasQuad(atlas, id, x, y, w, h, color, 3.0f); // alpha as coverage
}

void Painter2D::text(Text& font, std::string_view s, float x, float y, const glm::vec4& color, float scale)
{
	const unsigned atlas = font.atlas();
	if (!atlas || s.empty()) return;
	const Transform2D& t = transform();
	// a whole-pixel start keeps glyphs drawn at their own size pixel for pixel
	const glm::vec2 pen = glm::round(t.apply(glm::vec2(x, y)));
	glyphs_.clear();
	font.layout(s, pen.x, pen.y, scale * t.scale, glyphs_);
	if (glyphs_.empty()) return;
	useTexture(wrapped(atlas));
	for (const Text::Glyph& g : glyphs_)
	{
		const float hx = (g.x1 - g.x0) * 0.5f, hy = (g.y1 - g.y0) * 0.5f;
		emit(g.x0 + hx, g.y0 + hy, hx, hy, 1, 0, 0, 0, color, 0.0f, 2.0f, { g.u0, g.v0, g.u1, g.v1 });
	}
}

float Painter2D::text(ShapedText& font, std::string_view s, float x, float y, const glm::vec4& color)
{
	if (!font.ready() || s.empty()) return 0.0f;
	// shape() may grow or rebuild the atlas, which moves the glyphs' atlas coordinates:
	// quads still pending from this font are drawn before that happens
	if (tex_ && tex_.id == font.texture(*dev_).id) flush();
	const ShapedText::Line line = font.shape(s);
	if (line.glyphs.empty()) return line.advance;
	useTexture(font.texture(*dev_)); // after shape(): uploads the glyphs it added
	const Transform2D& t = transform();
	const glm::vec2 pen = glm::round(t.apply(glm::vec2(x, y)));
	for (const ShapedText::Glyph& g : line.glyphs)
	{
		const float hx = g.w * 0.5f * t.scale, hy = g.h * 0.5f * t.scale;
		emit(pen.x + g.x * t.scale + hx, pen.y + g.y * t.scale + hy, hx, hy, 1, 0, 0, 0, color, 0.0f,
		     g.color ? 4.0f : 3.0f, { g.u0, g.v0, g.u1, g.v1 });
	}
	return line.advance;
}

// --- state -----------------------------------------------------------------

void Painter2D::applyClip()
{
	if (clips_.empty())
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}
	const ClipRect& c = clips_.back();
	glEnable(GL_SCISSOR_TEST);
	glScissor(c.x, h_ - (c.y + c.h), c.w, c.h);
}

void Painter2D::pushClip(float x, float y, float w, float h)
{
	flush();
	const Rect r = transform().apply(Rect{ x, y, w, h });
	ClipRect c{ static_cast<int>(std::floor(r.x)), static_cast<int>(std::floor(r.y)),
	            static_cast<int>(std::ceil(r.x + r.w)), static_cast<int>(std::ceil(r.y + r.h)) };
	c.w -= c.x;
	c.h -= c.y;
	if (!clips_.empty())
	{
		const ClipRect& p = clips_.back();
		const int x0 = std::max(c.x, p.x), y0 = std::max(c.y, p.y);
		const int x1 = std::min(c.x + c.w, p.x + p.w), y1 = std::min(c.y + c.h, p.y + p.h);
		c = { x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0) };
	}
	c.w = std::max(c.w, 0);
	c.h = std::max(c.h, 0);
	clips_.push_back(c);
	applyClip();
}

void Painter2D::popClip()
{
	if (clips_.empty()) return;
	flush();
	clips_.pop_back();
	applyClip();
}

void Painter2D::pushTransform(glm::vec2 offset, float scale)
{
	xforms_.push_back(transform() * Transform2D{ offset, scale });
}

void Painter2D::popTransform()
{
	if (xforms_.size() > 1) xforms_.pop_back();
}

void Painter2D::pushAlpha(float alpha) { alpha_.push_back(alpha_.back() * alpha); }

void Painter2D::popAlpha()
{
	if (alpha_.size() > 1) alpha_.pop_back();
}

void Painter2D::pushDither(float strength, float cell)
{
	flush();
	dither_.push_back({ strength, std::max(cell, 1.0f) });
}

void Painter2D::popDither()
{
	if (dither_.empty()) return;
	flush();
	dither_.pop_back();
}

// --- programs ----------------------------------------------------------------

int Painter2D::addProgram(const std::string& fragPath)
{
	flush();
	Program p;
	// GLSL 330: the GL translation of the stock vertex stage, and the app's fragment shader
	p.shader = std::make_unique<Shader>(QS_GPU_SHADER_DIR "ui2d.vert.gl", fragPath.c_str());
	p.pipeline = gpu::gl::import(*dev_, p.shader->ID, pipelineDesc());
	p.ditherLoc = glGetUniformLocation(p.shader->ID, "uDither");
	p.shader->Activate();
	glUniform1i(glGetUniformLocation(p.shader->ID, "uTex"), 0);
	glUseProgram(0);
	programs_.push_back(std::move(p));
	return static_cast<int>(programs_.size()) - 1;
}

unsigned Painter2D::glProgram(int program) const
{
	const Program& p = programs_[program];
	return p.shader ? p.shader->ID : gpu::gl::program(*dev_, p.pipeline);
}

void Painter2D::bindTexture(int program, const char* sampler, unsigned texture)
{
	flush();
	Program& p = programs_[program];
	const int location = glGetUniformLocation(glProgram(program), sampler);
	for (Sampler& s : p.samplers)
		if (s.location == location) { s.texture = texture; return; }
	p.samplers.push_back({ location, texture });
	glUseProgram(glProgram(program));
	glUniform1i(location, static_cast<int>(p.samplers.size()));
	glUseProgram(0);
}

void Painter2D::pushProgram(int program)
{
	flush();
	programStack_.push_back(program);
	p0_ = p1_ = glm::vec4(0.0f);
}

void Painter2D::popProgram()
{
	if (programStack_.empty()) return;
	flush();
	programStack_.pop_back();
	p0_ = p1_ = glm::vec4(0.0f);
}

void Painter2D::params(const glm::vec4& p0, const glm::vec4& p1)
{
	p0_ = p0;
	p1_ = p1;
}

}
