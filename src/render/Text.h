#pragma once
// -----------------------------------------------------------------------------
// Text.h -- text drawing, on top of GlyphGL
// -----------------------------------------------------------------------------
// Draws flat text in pixel coordinates of whatever is bound: the window, or a
// Framebuffer when the text should end up as a texture on something in 3D.
// Only Text.cpp sees the GlyphGL headers, they redefine the gl* names.

#include <glm/glm.hpp>

#include <memory>
#include <string>

class Text
{
public:
	Text();
	~Text();

	// Rasterises printable ASCII from the font at pixelHeight. Throws when the
	// font can't be loaded, like Texture does for an image.
	void create(const std::string& fontPath, float pixelHeight);
	void Delete();
	bool ready() const;

	// size in pixels of the target about to be drawn into
	void setViewport(int width, int height);

	// x, y in pixels from the top-left; y is the baseline. Blends so the target's
	// alpha ends up as the text coverage, which is what a shader sampling a text
	// texture wants.
	void draw(const std::string& s, float x, float y, const glm::vec3& color, float scale = 1.0f);
	float width(const std::string& s, float scale = 1.0f) const;
	float lineHeight() const { return lineHeight_; }

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	float lineHeight_ = 18.0f;
};
