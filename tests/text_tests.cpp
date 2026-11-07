// Tests for ShapedText's GL-free half: font fallback, shaping, atlas packing.
// Uses only fonts bundled in fonts/, so it runs on any machine after clone.
#include "render/ShapedText.h"

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(...) do { \
	if (!(__VA_ARGS__)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); failures++; } \
} while (0)

int main()
{
	const std::string dir = QS_FONT_DIR;
	ShapedText t;
	// Space Mono has no Nerd Font icons; Meslo LGS NF has them
	t.create({ { dir + "SpaceMono-Bold.ttf" }, { dir + "MesloLGSNerdFontMono-Regular.ttf" } }, 20.0f);
	CHECK(t.ready());
	CHECK(t.fontCount() == 2);

	// fallback: plain Latin from the primary, an icon (nf-fa-home, U+F015) from the second
	CHECK(t.fontFor(U'A') == 0);
	CHECK(t.fontFor(U'ç') == 0);
	CHECK(t.fontFor(U'') == 1);
	// nothing has it: the primary, which draws its missing-glyph box
	CHECK(t.fontFor(U'\U0010FFFD') == 0);

	CHECK(t.ascent() > 0 && t.descent() > 0 && t.lineHeight() >= t.ascent());

	// shaping: a monospace font advances the same per character
	const float one = t.width("a");
	CHECK(one > 0);
	CHECK(std::fabs(t.width("abcd") - 4 * one) < 0.5f);
	CHECK(t.width("") == 0.0f);

	// combining accent (e + U+0301) stays one cluster: same advance as one char
	CHECK(std::fabs(t.width("e\xCC\x81") - one) < 0.5f);

	// a mixed run: text and an icon, every visible glyph in the atlas
	ShapedText::Line line = t.shape("Home \xEF\x80\x95");
	CHECK(line.glyphs.size() == 5); // H o m e + icon (space has no bitmap)
	for (const ShapedText::Glyph& g : line.glyphs)
	{
		CHECK(g.w > 0 && g.h > 0);
		CHECK(g.u0 >= 0 && g.v0 >= 0 && g.u1 <= 1 && g.v1 <= 1 && g.u0 < g.u1 && g.v0 < g.v1);
		CHECK(!g.color);
	}
	// glyphs above the baseline have a negative y
	CHECK(line.glyphs[0].y < 0);

	// malformed UTF-8 never crashes and still advances
	CHECK(t.width("\xFF\xFE") >= 0.0f);

	// many distinct glyphs: the atlas grows instead of dropping them
	std::string many;
	for (char32_t c = 0x21; c < 0x17F; c++)
	{
		if (c < 0x80) many += static_cast<char>(c);
		else { many += static_cast<char>(0xC0 | (c >> 6)); many += static_cast<char>(0x80 | (c & 0x3F)); }
	}
	ShapedText big;
	big.create({ { dir + "SpaceMono-Bold.ttf" } }, 96.0f);
	const ShapedText::Line l2 = big.shape(many);
	CHECK(!l2.glyphs.empty());
	CHECK(big.atlasSize() > 512);

	// Colour emoji strike (bitmap-only font): drawn near the asked size, not at the
	// strike's 109 px. Optional: runs only where Noto Color Emoji is installed.
	{
		const char* emoji = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf";
		if (FILE* f = std::fopen(emoji, "rb"))
		{
			std::fclose(f);
			ShapedText e;
			e.create({ { dir + "SpaceMono-Bold.ttf" }, { emoji } }, 20.0f);
			CHECK(e.fontFor(U'\U0001F642') == 1);
			const ShapedText::Line l = e.shape("\xF0\x9F\x99\x82"); // U+1F642
			CHECK(l.glyphs.size() == 1);
			if (!l.glyphs.empty())
			{
				CHECK(l.glyphs[0].color);
				CHECK(l.glyphs[0].h > 14 && l.glyphs[0].h < 30);
			}
			CHECK(l.advance > 14 && l.advance < 32);
		}
		else std::printf("text_tests: Noto Color Emoji not installed, emoji check skipped\n");
	}

	// a missing font is an error, not a crash
	bool threw = false;
	try { ShapedText bad; bad.create({ { dir + "no-such-font.ttf" } }, 12.0f); } catch (const std::exception&) { threw = true; }
	CHECK(threw);

	if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
	std::printf("text_tests: all passed\n");
	return 0;
}
