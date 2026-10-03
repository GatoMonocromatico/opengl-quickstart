// GlyphGL is header-only C with its own GL loader. GLAD comes first and
// GLYPH_NO_GL_LOADER makes GlyphGL call the GLAD functions instead.
#include <glad/glad.h>

#define GLYPH_NO_GL_LOADER
#define GLYPHGL_ATLAS_WIDTH 1024
#define GLYPHGL_ATLAS_HEIGHT 1024
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <glyphgl/glyph.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "render/Text.h"
#include <filesystem>
#include <stdexcept>

struct Text::Impl
{
	glyph_renderer_t renderer{};
};

Text::Text() = default;
Text::~Text() = default;

void Text::create(const std::string& fontPath, float pixelHeight)
{
	if (!std::filesystem::exists(fontPath))
	{
		throw std::runtime_error("Text: font not found: " + fontPath);
	}

	// every printable ASCII character
	std::string charset;
	for (char c = 32; c < 127; c++) charset += c;

	impl_ = std::make_unique<Impl>();
	impl_->renderer = glyph_renderer_create(fontPath.c_str(), pixelHeight, charset.c_str(), GLYPH_ENCODING_ASCII, nullptr, 0);
	if (!impl_->renderer.initialized)
	{
		impl_.reset();
		throw std::runtime_error("Text: GlyphGL could not load " + fontPath);
	}
	lineHeight_ = pixelHeight * 1.25f;
}

void Text::Delete()
{
	if (impl_) glyph_renderer_free(&impl_->renderer);
	impl_.reset();
}

bool Text::ready() const { return impl_ != nullptr; }

void Text::setViewport(int width, int height)
{
	if (impl_) glyph_renderer_set_projection(&impl_->renderer, width, height);
}

void Text::draw(const std::string& s, float x, float y, const glm::vec3& color, float scale)
{
	if (!impl_ || s.empty()) return;

	// alpha accumulates as coverage instead of being multiplied by itself
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glyph_renderer_draw_text(&impl_->renderer, s.c_str(), x, y, scale, color.r, color.g, color.b, 0);
	glEnable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

float Text::width(const std::string& s, float scale) const
{
	if (!impl_) return 0.0f;
	float w = 0.0f;
	for (unsigned char c : s)
	{
		glyph_atlas_char_t* ch = glyph_atlas_find_char(&impl_->renderer.atlas, c);
		if (ch) w += ch->advance * scale;
	}
	return w;
}
