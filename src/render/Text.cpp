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
#include "util/Utf8.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

struct Text::Impl
{
	glyph_renderer_t renderer{};
	// codepoint -> advance in pixels, so measuring doesn't scan the atlas
	std::unordered_map<uint32_t, float> advance;
	float fallback = 0.0f;
};

static bool skipped(uint32_t cp) { return cp < 32 || cp == 127; }

// What the atlas can't hold becomes '?' (what GlyphGL falls back to anyway), and
// control characters vanish. Also keeps malformed UTF-8 away from GlyphGL's decoder.
static std::string prepare(std::string_view s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size();)
	{
		size_t len;
		const uint32_t cp = utf8::decode(s, i, len);
		if (cp == utf8::kReplacement || cp > 0xFFFF) out += '?';
		else if (!skipped(cp)) out.append(s.substr(i, len));
		i += len;
	}
	return out;
}

static std::string charsetString(Text::Charset charset)
{
	std::string out;
	for (uint32_t c = 32; c < 127; c++) utf8::append(out, c);
	if (charset == Text::Charset::Ascii) return out;
	for (uint32_t c = 0xA0; c <= 0x17F; c++) utf8::append(out, c);
	for (uint32_t c : { 0x2713, 0x2717, 0x2192, 0x2190, 0x2022, 0x2026, 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019 })
		utf8::append(out, c);
	return out;
}

Text::Text() = default;
Text::~Text() = default;

void Text::create(const std::string& fontPath, float pixelHeight, Charset charset)
{
	if (!std::filesystem::exists(fontPath))
	{
		throw std::runtime_error("Text: font not found: " + fontPath);
	}

	const std::string chars = charsetString(charset);

	auto impl = std::make_unique<Impl>();
	impl->renderer = glyph_renderer_create(fontPath.c_str(), pixelHeight, chars.c_str(), GLYPH_ENCODING_UTF8, nullptr, 0);
	if (!impl->renderer.initialized)
	{
		throw std::runtime_error("Text: GlyphGL could not load " + fontPath);
	}

	const glyph_atlas_t& atlas = impl->renderer.atlas;
	for (int i = 0; i < atlas.num_chars; i++)
		impl->advance[static_cast<uint32_t>(atlas.chars[i].codepoint)] = static_cast<float>(atlas.chars[i].advance);
	auto q = impl->advance.find('?');
	impl->fallback = q != impl->advance.end() ? q->second : pixelHeight * 0.5f;

	// yoff is how far a glyph's top sits above the baseline
	ascent_ = pixelHeight * 0.8f;
	descent_ = pixelHeight * 0.2f;
	if (glyph_atlas_char_t* h = glyph_atlas_find_char(&impl->renderer.atlas, 'H'))
		if (h->height > 0) ascent_ = static_cast<float>(h->yoff);
	if (glyph_atlas_char_t* g = glyph_atlas_find_char(&impl->renderer.atlas, 'g'))
		if (g->height > 0) descent_ = std::max(0.0f, static_cast<float>(g->height - g->yoff));

	Delete();
	impl_ = std::move(impl);
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

void Text::draw(std::string_view s, float x, float y, const glm::vec3& color, float scale)
{
	if (!impl_ || s.empty()) return;
	const std::string safe = prepare(s);
	if (safe.empty()) return;

	// alpha accumulates as coverage instead of being multiplied by itself
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glyph_renderer_draw_text(&impl_->renderer, safe.c_str(), x, y, scale, color.r, color.g, color.b, 0);
	glEnable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// advance of one codepoint, matching what draw() does
static float advanceOf(const std::unordered_map<uint32_t, float>& adv, float fallback, uint32_t cp)
{
	if (skipped(cp) || cp > 0xFFFF) return cp > 0xFFFF ? fallback : 0.0f;
	auto it = adv.find(cp);
	return it != adv.end() ? it->second : fallback;
}

float Text::width(std::string_view s, float scale) const
{
	if (!impl_) return 0.0f;
	float w = 0.0f;
	for (size_t i = 0; i < s.size();)
	{
		size_t len;
		const uint32_t cp = utf8::decode(s, i, len);
		w += advanceOf(impl_->advance, impl_->fallback, cp == utf8::kReplacement ? '?' : cp);
		i += len;
	}
	return w * scale;
}

size_t Text::indexAt(std::string_view s, float x, float scale) const
{
	if (!impl_ || x <= 0.0f) return 0;
	float pos = 0.0f;
	for (size_t i = 0; i < s.size();)
	{
		size_t len;
		const uint32_t cp = utf8::decode(s, i, len);
		const float w = advanceOf(impl_->advance, impl_->fallback, cp == utf8::kReplacement ? '?' : cp) * scale;
		if (x < pos + w * 0.5f) return i;
		pos += w;
		i += len;
	}
	return s.size();
}
