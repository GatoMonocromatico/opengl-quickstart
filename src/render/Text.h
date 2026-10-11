#pragma once
// -----------------------------------------------------------------------------
// Text.h -- text drawing, on top of GlyphGL
// -----------------------------------------------------------------------------
// Draws flat text in pixel coordinates of whatever is bound: the window, or a
// Framebuffer when the text should end up as a texture on something in 3D.
// Only Text.cpp sees the GlyphGL headers, they redefine the gl* names.
// Strings are UTF-8. A codepoint the charset doesn't hold is drawn (and measured)
// as '?'; control characters and malformed bytes never crash, they draw nothing
// or '?'.

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <string_view>

class Text
{
public:
	Text();
	~Text();

	// Ascii: printable ASCII only (the default). Latin: ASCII, Latin-1 Supplement,
	// Latin Extended-A and a few symbols (check, cross, arrows, bullet, ellipsis,
	// dashes, curly quotes). Latin makes the atlas ~3x bigger.
	enum class Charset { Ascii, Latin };

	// Rasterises the charset from the font at pixelHeight. Throws when the
	// font can't be loaded, like Texture does for an image.
	void create(const std::string& fontPath, float pixelHeight, Charset charset = Charset::Ascii);
	void Delete();
	bool ready() const;

	// size in pixels of the target about to be drawn into
	void setViewport(int width, int height);

	// x, y in pixels from the top-left; y is the baseline. Blends so the target's
	// alpha ends up as the text coverage, which is what a shader sampling a text
	// texture wants.
	void draw(std::string_view s, float x, float y, const glm::vec3& color, float scale = 1.0f);
	// advance of the whole string (UTF-8 aware)
	float width(std::string_view s, float scale = 1.0f) const;
	float lineHeight() const { return lineHeight_; }

	// pixels a line's glyphs reach above / below the baseline (at scale 1); box
	// text with baseline - ascent() .. baseline + descent()
	float ascent(float scale = 1.0f) const { return ascent_ * scale; }
	float descent(float scale = 1.0f) const { return descent_ * scale; }

	// Byte offset of the caret boundary nearest to x (pixels from the string's
	// start). Always on a codepoint boundary; 0 for x <= 0, s.size() past the end.
	size_t indexAt(std::string_view s, float x, float scale = 1.0f) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	float lineHeight_ = 18.0f;
	float ascent_ = 14.0f;
	float descent_ = 4.0f;
};
