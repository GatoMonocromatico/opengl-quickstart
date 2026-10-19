#pragma once
// -----------------------------------------------------------------------------
// Painter2D.h -- batched 2D shapes and text in window pixels
// -----------------------------------------------------------------------------
// Coordinates are pixels of the target, origin top-left, y down. Shapes are
// batched and drawn when the batch has to be flushed (text, clip change, end()),
// so draw order is call order. Needs a GL context for create(); uses shaders/ui2d.*
// from QS_SHADER_DIR.
//
//   painter.begin(w, h);
//   painter.roundedRect(10, 10, 200, 40, 6, {0.2f, 0.3f, 0.5f, 1});
//   painter.text(font, "olá", 20, 38, {1, 1, 1, 1});
//   painter.end();

#include <glm/glm.hpp>

#include <memory>
#include <string_view>
#include <vector>

class Text;
class Shader;

namespace ui
{

class Painter2D
{
public:
	Painter2D();
	~Painter2D();

	// Throws on a shader error, like Shader does.
	void create();
	void Delete();

	// w, h = size in pixels of what is bound (the drawable size, not the window's
	// logical size on a high-DPI screen). Turns depth test off until end().
	void begin(int w, int h);
	void end();

	void rect(float x, float y, float w, float h, const glm::vec4& color);
	void roundedRect(float x, float y, float w, float h, float radius, const glm::vec4& color);
	// border drawn inside the rect's edge
	void outline(float x, float y, float w, float h, float thickness, const glm::vec4& color, float radius = 0.0f);
	// round caps; the line's width is thickness
	void line(float x0, float y0, float x1, float y1, float thickness, const glm::vec4& color);
	void circle(float cx, float cy, float radius, const glm::vec4& color);
	void circleOutline(float cx, float cy, float radius, float thickness, const glm::vec4& color);

	// A texture (a GL texture id) stretched over the rect, multiplied by tint. Drawn in
	// call order like the shapes (it flushes them first).
	void image(unsigned texture, float x, float y, float w, float h, const glm::vec4& tint = glm::vec4(1.0f));

	// y is the baseline. Only rgb is used, GlyphGL's shader has no alpha input;
	// mix the color toward the background for a dimmed look.
	void text(Text& font, std::string_view s, float x, float y, const glm::vec4& color, float scale = 1.0f);

	// Scissor to the rect, intersected with the current clip. Applies to shapes
	// and text. Every push needs a pop before end().
	void pushClip(float x, float y, float w, float h);
	void popClip();

	// Draws the pending shapes now (you rarely need to call it).
	void flush();

	int width() const { return w_; }
	int height() const { return h_; }

private:
	struct ClipRect { int x, y, w, h; };
	void quad(float cx, float cy, float hx, float hy, float ux, float uy, float radius, float border, const glm::vec4& color);
	void applyClip();

	std::unique_ptr<Shader> shader_;
	unsigned vao_ = 0;
	unsigned vbo_ = 0;
	int screenLoc_ = -1;
	int texturedLoc_ = -1;
	std::vector<float> verts_;
	std::vector<ClipRect> clips_;
	int w_ = 0, h_ = 0;
	bool depthWasOn_ = false;
};

}
