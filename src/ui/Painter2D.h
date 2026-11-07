#pragma once
// -----------------------------------------------------------------------------
// Painter2D.h -- batched 2D shapes, images and text in window pixels
// -----------------------------------------------------------------------------
// Coordinates are pixels of the target, origin top-left, y down, through the
// current transform. Everything goes into one batch that is drawn when it has to
// be flushed (a clip, program or dither change, another texture, end()), so draw
// order is call order. It draws through gpu:: (shaders/gpu/ui2d.*, built by
// qs_gpu_shaders) into whatever framebuffer is bound, on the GL backend; needs a
// GL context for create().
//
//   painter.begin(w, h);
//   painter.roundedRect(10, 10, 200, 40, 6, {0.2f, 0.3f, 0.5f, 1});
//   painter.text(font, "olá", 20, 38, {1, 1, 1, 1});
//   painter.end();
//
// The state is a stack each (clip, transform, alpha, dither, program): every push
// needs its pop before end().

#include "gpu/Gpu.h"
#include "ui/Geometry.h"

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "render/Text.h"

class Shader;
class ShapedText;

namespace ui
{

class Painter2D
{
public:
	Painter2D();
	~Painter2D();

	// Throws on a shader error, like Shader does. create() makes its own device on the
	// current GL context; create(device) draws through the app's.
	void create();
	void create(gpu::Device& device);
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

	// A texture (a GL texture id) over the rect, multiplied by tint. uv is the part of it
	// shown: (u0, v0) at the rect's top-left, (u1, v1) at its bottom-right, so v0 > v1
	// turns it over (a Framebuffer or a ScreenCapture holds its bottom row first).
	void image(unsigned texture, float x, float y, float w, float h, const glm::vec4& tint = glm::vec4(1.0f),
	           const glm::vec4& uv = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

	// y is the baseline; the start lands on a whole pixel. The color's alpha counts.
	void text(Text& font, std::string_view s, float x, float y, const glm::vec4& color, float scale = 1.0f);
	// Unicode text (any script, emoji, icon fonts) through ShapedText's fallback chain,
	// in the same batch and under the same transform, alpha, dither and program as the
	// rest. y is the baseline; colour glyphs (emoji) keep their colours and only fade
	// with the colour's alpha. Returns the pen advance (untransformed).
	float text(ShapedText& font, std::string_view s, float x, float y, const glm::vec4& color);

	// --- state ---------------------------------------------------------------
	// Scissor to the rect (through the transform), intersected with the current clip.
	void pushClip(float x, float y, float w, float h);
	void popClip();
	// What's drawn after it lands at offset + p * scale, inside the current transform:
	// positions, sizes, corner radii, line widths, text and clips.
	void pushTransform(glm::vec2 offset, float scale = 1.0f);
	void popTransform();
	const Transform2D& transform() const { return xforms_.back(); }
	// Multiplies the alpha of what's drawn after it; nested ones multiply.
	void pushAlpha(float alpha);
	void popAlpha();
	// Ordered dithering: what's drawn after it shows only on the pixels of a 4x4 Bayer
	// pattern below strength (0.5 = a checkerboard, 1 = all of it), cell px a cell.
	void pushDither(float strength, float cell = 1.0f);
	void popDither();

	// --- an app's own fragment shader over the same quads --------------------
	// GL only. Links fragPath (GLSL 330) to Painter2D's vertex stage. That shader can
	// `#include <ui2d.glsl>` for what each quad carries and the coverage, uv and
	// dither helpers (see that file). Returns the program's id. Throws on a shader
	// error, like Shader.
	int addProgram(const std::string& fragPath);
	// Its GL program, for its own uniforms. They're read when the batch draws, so set
	// them before the program's first quad in a frame, or flush() first.
	unsigned glProgram(int program) const;
	// A texture for one of its samplers, bound whenever it draws (units 1 and up;
	// unit 0 is the painter's uTex).
	void bindTexture(int program, const char* sampler, unsigned texture);
	// Shapes, images and text after it are drawn with the program, each quad carrying
	// the params of the moment (the shader's vParam0, vParam1). Both start at 0.
	void pushProgram(int program);
	void popProgram();
	void params(const glm::vec4& p0, const glm::vec4& p1 = glm::vec4(0.0f));

	// Draws the pending batch now (you rarely need to call it).
	void flush();

	int width() const { return w_; }
	int height() const { return h_; }

private:
	struct ClipRect { int x, y, w, h; };
	struct Sampler { int location; unsigned texture; };
	struct Program
	{
		std::unique_ptr<Shader> shader; // an app's (GL); none for the stock one
		gpu::Pipeline pipeline;
		int ditherLoc = -1;
		std::vector<Sampler> samplers;  // on units 1, 2, ...
	};

	gpu::PipelineDesc pipelineDesc() const;
	// the gpu handle for a GL texture id, made once
	gpu::Texture wrapped(unsigned texture);
	// through the transform
	void quad(float cx, float cy, float hx, float hy, float ux, float uy, float radius, float border, const glm::vec4& color,
	          float mode = 0.0f, const glm::vec4& uv = glm::vec4(0.0f));
	// in target pixels; pad grows the quad for the antialiasing pixel (0 for glyphs)
	void emit(float cx, float cy, float hx, float hy, float ux, float uy, float radius, float border, const glm::vec4& color,
	          float pad, float mode, const glm::vec4& uv);
	// unit 0 for what's pending; another texture flushes first
	void useTexture(unsigned texture);
	void applyClip();

	gpu::Device* dev_ = nullptr;
	std::unique_ptr<gpu::Device> ownDevice_;  // create() without one
	gpu::Shader vs_, fs_;
	std::vector<Program> programs_;  // [0] is gpu/ui2d.frag
	gpu::Buffer vb_;
	uint32_t vbOffset_ = 0;          // where the next batch goes: the buffer is a ring
	std::map<unsigned, gpu::Texture> wrapped_;
	std::vector<float> verts_;
	std::vector<Text::Glyph> glyphs_;
	unsigned tex_ = 0;
	std::vector<ClipRect> clips_;
	std::vector<Transform2D> xforms_{ Transform2D{} };
	std::vector<float> alpha_{ 1.0f };
	std::vector<glm::vec2> dither_;
	std::vector<int> programStack_;
	glm::vec4 p0_{ 0.0f }, p1_{ 0.0f };
	int w_ = 0, h_ = 0;
	bool depthWasOn_ = false;
};

}
