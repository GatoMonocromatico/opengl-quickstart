#include "ui/Painter2D.h"
#include "render/Text.h"
#include "render/shaderClass.h"

#include <algorithm>
#include <cmath>

namespace ui
{

Painter2D::Painter2D() = default;
Painter2D::~Painter2D() = default;

// floats per vertex: pos(2) color(4) local(2) shape(4) uvRect(4) mode(1) param0(4) param1(4)
static constexpr int kFloats = 25;
static constexpr int kSizes[8] = { 2, 4, 2, 4, 4, 1, 4, 4 };

Painter2D::Program Painter2D::makeProgram(const std::string& fragPath)
{
	Program p;
	p.shader = std::make_unique<Shader>(QS_SHADER_DIR "ui2d.vert", fragPath.c_str());
	p.screenLoc = glGetUniformLocation(p.shader->ID, "uScreen");
	p.ditherLoc = glGetUniformLocation(p.shader->ID, "uDither");
	p.shader->Activate();
	glUniform1i(glGetUniformLocation(p.shader->ID, "uTex"), 0);
	return p;
}

void Painter2D::create()
{
	Delete();
	programs_.push_back(makeProgram(QS_SHADER_DIR "ui2d.frag"));

	glGenVertexArrays(1, &vao_);
	glGenBuffers(1, &vbo_);
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	const GLsizei stride = kFloats * sizeof(float);
	size_t offset = 0;
	for (int i = 0; i < 8; i++)
	{
		glVertexAttribPointer(i, kSizes[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offset * sizeof(float)));
		glEnableVertexAttribArray(i);
		offset += kSizes[i];
	}
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Painter2D::Delete()
{
	for (Program& p : programs_)
		if (p.shader) p.shader->Delete();
	programs_.clear();
	if (vbo_) glDeleteBuffers(1, &vbo_);
	if (vao_) glDeleteVertexArrays(1, &vao_);
	vbo_ = vao_ = 0;
	verts_.clear();
}

void Painter2D::begin(int w, int h)
{
	w_ = w;
	h_ = h;
	verts_.clear();
	tex_ = 0;
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
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	// colour blends over; alpha adds up as coverage, so a transparent target ends up right too
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	p.shader->Activate();
	glUniform2f(p.screenLoc, static_cast<float>(w_), static_cast<float>(h_));
	const glm::vec2 d = dither_.empty() ? glm::vec2(1.0f) : dither_.back();
	glUniform2f(p.ditherLoc, d.x, d.y);
	for (size_t i = 0; i < p.samplers.size(); i++)
	{
		glActiveTexture(GL_TEXTURE1 + static_cast<GLenum>(i));
		glBindTexture(GL_TEXTURE_2D, p.samplers[i].texture);
	}
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex_);
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(float), verts_.data(), GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size() / kFloats));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	verts_.clear();
	tex_ = 0;
}

void Painter2D::useTexture(unsigned texture)
{
	if (texture != tex_ && tex_ != 0) flush();
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
	if (w <= 0 || h <= 0 || !texture) return;
	useTexture(texture);
	quad(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, 1, 0, 0, 0, tint, 1.0f, uv);
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
	useTexture(atlas);
	for (const Text::Glyph& g : glyphs_)
	{
		const float hx = (g.x1 - g.x0) * 0.5f, hy = (g.y1 - g.y0) * 0.5f;
		emit(g.x0 + hx, g.y0 + hy, hx, hy, 1, 0, 0, 0, color, 0.0f, 2.0f, { g.u0, g.v0, g.u1, g.v1 });
	}
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
	programs_.push_back(makeProgram(fragPath));
	return static_cast<int>(programs_.size()) - 1;
}

unsigned Painter2D::glProgram(int program) const { return programs_[program].shader->ID; }

void Painter2D::bindTexture(int program, const char* sampler, unsigned texture)
{
	flush();
	Program& p = programs_[program];
	const int location = glGetUniformLocation(p.shader->ID, sampler);
	for (Sampler& s : p.samplers)
		if (s.location == location) { s.texture = texture; return; }
	p.samplers.push_back({ location, texture });
	p.shader->Activate();
	glUniform1i(location, static_cast<int>(p.samplers.size()));
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
