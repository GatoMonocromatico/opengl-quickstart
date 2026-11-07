#pragma once
// -----------------------------------------------------------------------------
// ShapedText.h -- Unicode text: HarfBuzz shaping, FreeType glyphs, font fallback
// -----------------------------------------------------------------------------
// Text (GlyphGL) covers a Latin charset from one font. ShapedText takes a list of
// fonts and shapes any UTF-8 string: ligatures, combining accents, CJK, icon fonts
// and colour emoji (PNG strikes such as Noto Color Emoji). Each codepoint uses the
// first font in the list that has it, and the string is shaped run by run, so the
// usual list is: the UI font, then symbols, emoji and CJK fallbacks. Joiners,
// variation selectors, skin-tone modifiers and combining marks stay in the run of
// the character before them. Left-to-right only (no bidi yet).
//
// Glyphs are rasterised on demand into one RGBA atlas, premultiplied: coverage
// glyphs are white (tinted when drawn), colour glyphs keep their colours. Draw with
// ui::Painter2D::text(ShapedText&, ...). Shaping and rasterising need no GL; only
// texture() touches GL, uploading the atlas when it changed.
//
//   ShapedText t;
//   t.create({{"/usr/share/fonts/.../Recursive.ttc"}, {".../NotoColorEmoji.ttf"}}, 18.0f);
//   painter.text(t, "olá 🙂 日本", 20, 40, {1, 1, 1, 1});

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ShapedText
{
public:
	struct FontSpec
	{
		std::string path;
		int faceIndex = 0; // face inside a .ttc collection
	};

	// One glyph quad in pixels relative to the pen origin (x right, y down, the
	// baseline at y = 0), with its rectangle in the atlas in 0..1.
	struct Glyph
	{
		float x, y, w, h;
		float u0, v0, u1, v1;
		bool color;
	};

	// A shaped string. Its atlas coordinates stay valid until the next shape()
	// call (the atlas can grow or be rebuilt then).
	struct Line
	{
		std::vector<Glyph> glyphs;
		float advance = 0.0f; // pen advance of the whole string
	};

	ShapedText();
	~ShapedText();
	ShapedText(const ShapedText&) = delete;
	ShapedText& operator=(const ShapedText&) = delete;

	// fonts[0] is the primary font and sets the line metrics. Throws when a font
	// can't be opened. pixelHeight is the em size in pixels.
	void create(const std::vector<FontSpec>& fonts, float pixelHeight);
	void Delete();
	bool ready() const;

	Line shape(std::string_view utf8);
	// advance of the string, without rasterising anything
	float width(std::string_view utf8);

	float pixelHeight() const;
	float ascent() const;     // pixels above the baseline (primary font)
	float descent() const;    // pixels below the baseline, positive
	float lineHeight() const; // baseline to baseline

	// Index of the font a codepoint is drawn with: the first one that has it, or 0
	// (which then draws its "missing glyph" box).
	int fontFor(char32_t cp) const;
	int fontCount() const;

	// GL texture name of the atlas; uploads pending glyphs first. Needs a context.
	unsigned texture();
	int atlasSize() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
