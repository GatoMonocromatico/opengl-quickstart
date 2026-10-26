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
	GLint opacityAt = -1;
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

// GlyphGL's own fragment shader with an opacity on top, so draw can fade text
static const std::string& fadingFragmentShader()
{
	static const std::string source = [] {
		std::string s = glyph__get_fragment_shader_source_cached();
		const std::string colour = "uniform vec3 textColor;\n";
		s.replace(s.find(colour), colour.size(), colour + "uniform float opacity = 1.0;\n");
		const std::string out = "FragColor = vec4(textColor, alpha);";
		s.replace(s.find(out), out.size(), "FragColor = vec4(textColor, alpha * opacity);");
		return s;
	}();
	return source;
}

// GlyphGL's rasteriser puts each edge's whole coverage in one pixel, so a stroke thinner than a
// pixel, both its edges in the same one, cancels out: small text loses strokes ('l' becomes a dot,
// 'p' an 'o'). A small atlas is redrawn from the same rasteriser at 4x, each pixel the mean of its
// 4x4 (Libraries/ stays untouched)
static void supersample(const glyph_renderer_t& r, const std::string& fontPath, float pixelHeight)
{
	glyph_font_t font{};
	if (!glyph_ttf_load_font_from_file(&font, fontPath.c_str())) return;
	const float scale = glyph_ttf_scale_for_pixel_height(&font, pixelHeight) * 4.0f;
	glBindTexture(GL_TEXTURE_2D, r.texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	std::vector<unsigned char> out;
	for (int i = 0; i < r.atlas.num_chars; i++)
	{
		const glyph_atlas_char_t& ch = r.atlas.chars[i];
		if (ch.width <= 0 || ch.height <= 0) continue;
		int w = 0, h = 0, xoff = 0, yoff = 0;
		unsigned char* big = glyph_ttf_get_glyph_bitmap(&font, glyph_ttf_find_glyph_index(&font, ch.codepoint), scale, scale, &w, &h, &xoff, &yoff);
		if (!big) continue;
		// the 4x bitmap starts where the 1x one does (the outline's left and top), so blocks line up
		out.assign(static_cast<size_t>(ch.width) * ch.height, 0);
		for (int y = 0; y < ch.height; y++)
			for (int x = 0; x < ch.width; x++)
			{
				int sum = 0;
				for (int sy = 0; sy < 4; sy++)
					for (int sx = 0; sx < 4; sx++)
						if (x * 4 + sx < w && y * 4 + sy < h) sum += big[(y * 4 + sy) * w + x * 4 + sx];
				out[static_cast<size_t>(y) * ch.width + x] = static_cast<unsigned char>(sum / 16);
			}
		glyph_ttf_free_bitmap(big);
		glTexSubImage2D(GL_TEXTURE_2D, 0, ch.x, ch.y, ch.width, ch.height, GL_RED, GL_UNSIGNED_BYTE, out.data());
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glyph_ttf_free_font(&font);
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
	// any type but none makes GlyphGL take these shaders, it draws the same either way
	glyph_effect_t fading = { GLYPH_EFFECT_GLOW, glyph__get_vertex_shader_source_cached(), fadingFragmentShader().c_str() };
	impl->renderer = glyph_renderer_create(fontPath.c_str(), pixelHeight, chars.c_str(), GLYPH_ENCODING_UTF8, &fading, 0);
	if (!impl->renderer.initialized)
	{
		throw std::runtime_error("Text: GlyphGL could not load " + fontPath);
	}
	impl->opacityAt = glGetUniformLocation(impl->renderer.shader, "opacity");
	// from about 24 px every stroke is wider than a pixel
	if (pixelHeight <= 24.0f) supersample(impl->renderer, fontPath, pixelHeight);

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

void Text::draw(std::string_view s, float x, float y, const glm::vec3& color, float scale, float opacity)
{
	if (!impl_ || s.empty()) return;
	const std::string safe = prepare(s);
	if (safe.empty()) return;

	// alpha accumulates as coverage instead of being multiplied by itself
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	glUseProgram(impl_->renderer.shader);
	glUniform1f(impl_->opacityAt, opacity);
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

// the same walk glyph_renderer_draw_text does, minus the drawing
void Text::layout(std::string_view s, float x, float y, float scale, std::vector<Glyph>& out) const
{
	if (!impl_ || s.empty()) return;
	const std::string safe = prepare(s);
	glyph_atlas_t& atlas = impl_->renderer.atlas;
	const float aw = static_cast<float>(atlas.image.width), ah = static_cast<float>(atlas.image.height);
	float pen = x;
	for (size_t i = 0; i < safe.size();)
	{
		size_t len;
		const uint32_t cp = utf8::decode(safe, i, len);
		i += len;
		glyph_atlas_char_t* ch = glyph_atlas_find_char(&atlas, static_cast<int>(cp));
		if (!ch) ch = glyph_atlas_find_char(&atlas, '?');
		if (!ch || ch->width == 0)
		{
			pen += ch ? static_cast<float>(ch->advance) * scale : atlas.pixel_height * 0.5f * scale;
			continue;
		}
		const float x0 = pen + static_cast<float>(ch->xoff) * scale;
		const float y0 = y - static_cast<float>(ch->yoff) * scale;
		out.push_back({ x0, y0, x0 + static_cast<float>(ch->width) * scale, y0 + static_cast<float>(ch->height) * scale,
		                static_cast<float>(ch->x) / aw, static_cast<float>(ch->y) / ah,
		                static_cast<float>(ch->x + ch->width) / aw, static_cast<float>(ch->y + ch->height) / ah });
		pen += static_cast<float>(ch->advance) * scale;
	}
}

unsigned Text::atlas() const { return impl_ ? impl_->renderer.texture : 0; }

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
