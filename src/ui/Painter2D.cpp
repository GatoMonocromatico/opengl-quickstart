#include "ui/Painter2D.h"
#include "render/Text.h"
#include "render/shaderClass.h"

#include <algorithm>
#include <cmath>

namespace ui
{

Painter2D::Painter2D() = default;
Painter2D::~Painter2D() = default;

// floats per vertex: pos(2) color(4) local(2) shape(4)
static constexpr int kFloats = 12;

void Painter2D::create()
{
	Delete();
	shader_ = std::make_unique<Shader>(QS_SHADER_DIR "ui2d.vert", QS_SHADER_DIR "ui2d.frag");
	screenLoc_ = glGetUniformLocation(shader_->ID, "uScreen");

	glGenVertexArrays(1, &vao_);
	glGenBuffers(1, &vbo_);
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	const GLsizei stride = kFloats * sizeof(float);
	const int sizes[4] = { 2, 4, 2, 4 };
	size_t offset = 0;
	for (int i = 0; i < 4; i++)
	{
		glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offset * sizeof(float)));
		glEnableVertexAttribArray(i);
		offset += sizes[i];
	}
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Painter2D::Delete()
{
	if (shader_) shader_->Delete();
	shader_.reset();
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
	clips_.clear();
	depthWasOn_ = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Painter2D::end()
{
	flush();
	clips_.clear();
	glDisable(GL_SCISSOR_TEST);
	if (depthWasOn_) glEnable(GL_DEPTH_TEST);
}

void Painter2D::flush()
{
	if (verts_.empty() || !shader_) { verts_.clear(); return; }
	glDisable(GL_DEPTH_TEST); // Text::draw turns it back on
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	shader_->Activate();
	glUniform2f(screenLoc_, static_cast<float>(w_), static_cast<float>(h_));
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(float), verts_.data(), GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size() / kFloats));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	verts_.clear();
}

// One quad centered at (cx, cy) with half size (hx, hy), rotated so its x axis
// is (ux, uy). The quad is grown by a pixel for the antialiasing fringe.
void Painter2D::quad(float cx, float cy, float hx, float hy, float ux, float uy, float radius, float border, const glm::vec4& color)
{
	const float pad = 1.0f;
	const float ex = hx + pad, ey = hy + pad;
	const float nx = -uy, ny = ux;
	const float lx[4] = { -ex, ex, ex, -ex };
	const float ly[4] = { -ey, -ey, ey, ey };
	const int order[6] = { 0, 1, 2, 0, 2, 3 };
	for (int k : order)
	{
		const float x = cx + ux * lx[k] + nx * ly[k];
		const float y = cy + uy * lx[k] + ny * ly[k];
		const float v[kFloats] = { x, y, color.r, color.g, color.b, color.a, lx[k], ly[k], hx, hy, radius, border };
		verts_.insert(verts_.end(), v, v + kFloats);
	}
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

void Painter2D::text(Text& font, std::string_view s, float x, float y, const glm::vec4& color, float scale)
{
	flush();
	font.setViewport(w_, h_);
	font.draw(s, x, y, glm::vec3(color), scale);
}

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
	ClipRect c{ static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)),
	            static_cast<int>(std::ceil(x + w)), static_cast<int>(std::ceil(y + h)) };
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

}
