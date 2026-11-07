#include "render/ShapedText.h"
#include "util/Utf8.h"

#include "gpu/Gpu.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace
{

// Codepoints that belong to the cluster before them, so they are shaped with that
// cluster's font whatever font covers them on their own: emoji sequences (ZWJ,
// variation selectors, skin tones, flag tags) and combining marks.
bool sticksToPrevious(char32_t c)
{
	return c == 0x200D || c == 0x200C
		|| (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xE0100 && c <= 0xE01EF)
		|| (c >= 0x1F3FB && c <= 0x1F3FF)
		|| (c >= 0xE0020 && c <= 0xE007F)
		|| (c >= 0x0300 && c <= 0x036F) || (c >= 0x1AB0 && c <= 0x1AFF)
		|| (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x20D0 && c <= 0x20FF)
		|| (c >= 0xFE20 && c <= 0xFE2F);
}

constexpr int kPad = 1;          // empty pixels around each glyph in the atlas
constexpr int kFirstAtlas = 512; // grows by doubling
constexpr int kMaxAtlas = 4096;  // past this the atlas is rebuilt from scratch

// Box-filter resize of a premultiplied RGBA image (colour emoji strikes are much
// bigger than UI text, e.g. 136 px for Noto Color Emoji).
std::vector<uint8_t> resize(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh)
{
	std::vector<uint8_t> dst(static_cast<size_t>(dw) * dh * 4);
	for (int dy = 0; dy < dh; dy++)
	{
		const int y0 = dy * sh / dh, y1 = std::max(y0 + 1, (dy + 1) * sh / dh);
		for (int dx = 0; dx < dw; dx++)
		{
			const int x0 = dx * sw / dw, x1 = std::max(x0 + 1, (dx + 1) * sw / dw);
			unsigned sum[4] = { 0, 0, 0, 0 };
			for (int y = y0; y < y1; y++)
				for (int x = x0; x < x1; x++)
					for (int c = 0; c < 4; c++) sum[c] += src[(static_cast<size_t>(y) * sw + x) * 4 + c];
			const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
			for (int c = 0; c < 4; c++) dst[(static_cast<size_t>(dy) * dw + dx) * 4 + c] = static_cast<uint8_t>(sum[c] / n);
		}
	}
	return dst;
}

}

struct ShapedText::Impl
{
	struct Face
	{
		FT_Face ft = nullptr;
		hb_font_t* hb = nullptr;
		float scale = 1.0f;      // strike pixels -> requested pixels (bitmap-only fonts)
		bool bitmapOnly = false; // colour emoji strikes: no outlines, fixed sizes
		FT_Int32 loadFlags = 0;
	};
	struct Entry
	{
		int x = 0, y = 0, w = 0, h = 0; // atlas pixels; w == 0 for an empty glyph (space)
		float left = 0, top = 0;        // bitmap offset from the pen, y up
		bool color = false;
	};

	FT_Library lib = nullptr;
	std::vector<Face> faces;
	hb_buffer_t* buf = nullptr;
	float px = 0, asc = 0, desc = 0, lh = 0;

	int size = 0;
	std::vector<uint8_t> pixels; // RGBA, premultiplied
	int shelfX = kPad, shelfY = kPad, shelfH = 0;
	std::unordered_map<uint64_t, Entry> cache;
	bool dirty = false;
	gpu::Device* dev = nullptr; // where tex lives
	gpu::Texture tex;
	int texSize = 0;

	~Impl()
	{
		for (Face& f : faces)
		{
			if (f.hb) hb_font_destroy(f.hb);
			if (f.ft) FT_Done_Face(f.ft);
		}
		if (buf) hb_buffer_destroy(buf);
		if (lib) FT_Done_FreeType(lib);
	}

	int fontFor(char32_t c) const
	{
		for (size_t i = 0; i < faces.size(); i++)
			if (FT_Get_Char_Index(faces[i].ft, c)) return static_cast<int>(i);
		return 0;
	}

	void resetAtlas(int s)
	{
		size = s;
		pixels.assign(static_cast<size_t>(size) * size * 4, 0);
		shelfX = shelfY = kPad;
		shelfH = 0;
		cache.clear();
		dirty = true;
	}

	// Doubles the atlas, keeping every glyph where it is (pixel positions don't
	// move, only their 0..1 coordinates change).
	bool grow()
	{
		if (size >= kMaxAtlas) return false;
		const int n = size * 2;
		std::vector<uint8_t> p(static_cast<size_t>(n) * n * 4, 0);
		for (int y = 0; y < size; y++)
			std::memcpy(&p[static_cast<size_t>(y) * n * 4], &pixels[static_cast<size_t>(y) * size * 4], static_cast<size_t>(size) * 4);
		pixels.swap(p);
		size = n;
		dirty = true;
		return true;
	}

	// Shelf packing. False when even the largest atlas is full.
	bool place(int w, int h, int& ox, int& oy)
	{
		for (;;)
		{
			if (shelfX + w + kPad > size) { shelfY += shelfH + kPad; shelfX = kPad; shelfH = 0; }
			if (w + 2 * kPad <= size && shelfY + h + kPad <= size)
			{
				ox = shelfX; oy = shelfY;
				shelfX += w + kPad;
				shelfH = std::max(shelfH, h);
				return true;
			}
			if (!grow()) return false;
		}
	}

	// The atlas entry for a glyph, rasterising it on first use. Null with overflow
	// set when the atlas is full.
	const Entry* glyph(int fi, unsigned gid, bool& overflow)
	{
		const uint64_t key = (static_cast<uint64_t>(fi) << 32) | gid;
		if (auto it = cache.find(key); it != cache.end()) return &it->second;

		Face& f = faces[static_cast<size_t>(fi)];
		Entry e;
		if (FT_Load_Glyph(f.ft, gid, f.loadFlags) == 0)
		{
			FT_GlyphSlot slot = f.ft->glyph;
			if (slot->format != FT_GLYPH_FORMAT_BITMAP) FT_Render_Glyph(slot, FT_RENDER_MODE_LIGHT);
			const FT_Bitmap& b = slot->bitmap;
			const int sw = static_cast<int>(b.width), sh = static_cast<int>(b.rows);
			if (sw > 0 && sh > 0 && b.buffer)
			{
				std::vector<uint8_t> rgba(static_cast<size_t>(sw) * sh * 4);
				const bool color = b.pixel_mode == FT_PIXEL_MODE_BGRA;
				for (int y = 0; y < sh; y++)
				{
					const unsigned char* row = b.pitch >= 0 ? b.buffer + static_cast<ptrdiff_t>(y) * b.pitch
						: b.buffer + static_cast<ptrdiff_t>(sh - 1 - y) * -b.pitch;
					for (int x = 0; x < sw; x++)
					{
						uint8_t* o = &rgba[(static_cast<size_t>(y) * sw + x) * 4];
						if (color) { o[0] = row[x * 4 + 2]; o[1] = row[x * 4 + 1]; o[2] = row[x * 4 + 0]; o[3] = row[x * 4 + 3]; }
						else
						{
							const uint8_t a = b.pixel_mode == FT_PIXEL_MODE_MONO
								? static_cast<uint8_t>(((row[x >> 3] >> (7 - (x & 7))) & 1) * 255) : row[x];
							o[0] = o[1] = o[2] = o[3] = a;
						}
					}
				}
				int dw = sw, dh = sh;
				if (f.scale != 1.0f)
				{
					dw = std::max(1, static_cast<int>(std::lround(sw * f.scale)));
					dh = std::max(1, static_cast<int>(std::lround(sh * f.scale)));
					rgba = resize(rgba, sw, sh, dw, dh);
				}
				int ox = 0, oy = 0;
				if (!place(dw, dh, ox, oy)) { overflow = true; return nullptr; }
				for (int y = 0; y < dh; y++)
					std::memcpy(&pixels[(static_cast<size_t>(oy + y) * size + ox) * 4], &rgba[static_cast<size_t>(y) * dw * 4], static_cast<size_t>(dw) * 4);
				dirty = true;
				e = { ox, oy, dw, dh, slot->bitmap_left * f.scale, slot->bitmap_top * f.scale, color };
			}
		}
		return &(cache[key] = e);
	}

	Line shape(std::string_view s, bool raster, bool& overflow)
	{
		Line line;
		float pen = 0.0f;
		size_t i = 0;
		while (i < s.size())
		{
			// a run: the longest stretch drawn with one font
			const size_t start = i;
			size_t len = 0;
			const int fi = fontFor(utf8::decode(s, i, len));
			i += len;
			while (i < s.size())
			{
				const char32_t c = utf8::decode(s, i, len);
				if (!sticksToPrevious(c) && fontFor(c) != fi) break;
				i += len;
			}

			const Face& f = faces[static_cast<size_t>(fi)];
			hb_buffer_clear_contents(buf);
			// the whole string goes in as context, only [start, i) gets shaped
			hb_buffer_add_utf8(buf, s.data(), static_cast<int>(s.size()), static_cast<unsigned>(start), static_cast<int>(i - start));
			hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
			hb_buffer_guess_segment_properties(buf);
			hb_shape(f.hb, buf, nullptr, 0);

			unsigned n = 0;
			const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
			const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
			const float k = f.scale / 64.0f; // HarfBuzz-on-FreeType positions are 26.6
			for (unsigned j = 0; j < n; j++)
			{
				if (raster)
				{
					const Entry* e = glyph(fi, info[j].codepoint, overflow);
					if (overflow) return line;
					if (e && e->w > 0)
					{
						const float gx = std::round(pen + pos[j].x_offset * k + e->left);
						const float gy = std::round(-pos[j].y_offset * k - e->top);
						// atlas pixels for now, turned into 0..1 once the atlas size is final
						line.glyphs.push_back({ gx, gy, static_cast<float>(e->w), static_cast<float>(e->h),
							static_cast<float>(e->x), static_cast<float>(e->y),
							static_cast<float>(e->x + e->w), static_cast<float>(e->y + e->h), e->color });
					}
				}
				pen += pos[j].x_advance * k;
			}
		}
		line.advance = pen;
		const float inv = 1.0f / static_cast<float>(size);
		for (Glyph& g : line.glyphs) { g.u0 *= inv; g.v0 *= inv; g.u1 *= inv; g.v1 *= inv; }
		return line;
	}
};

ShapedText::ShapedText() = default;
ShapedText::~ShapedText() { Delete(); }

void ShapedText::create(const std::vector<FontSpec>& fonts, float pixelHeight)
{
	Delete();
	if (fonts.empty()) throw std::invalid_argument("ShapedText: no fonts given");
	if (!(pixelHeight > 0.0f)) throw std::invalid_argument("ShapedText: pixelHeight must be > 0");

	auto m = std::make_unique<Impl>();
	if (FT_Init_FreeType(&m->lib)) throw std::runtime_error("ShapedText: FreeType failed to start");
	m->px = pixelHeight;

	for (const FontSpec& spec : fonts)
	{
		FT_Face ft = nullptr;
		if (FT_New_Face(m->lib, spec.path.c_str(), spec.faceIndex, &ft))
			throw std::runtime_error("ShapedText: can't open font " + spec.path + " (face " + std::to_string(spec.faceIndex) + ")");
		Impl::Face face;
		face.ft = ft;
		m->faces.push_back(face); // owned by Impl from here, even if a later step throws
		Impl::Face& f = m->faces.back();

		if (FT_IS_SCALABLE(ft))
		{
			FT_Set_Pixel_Sizes(ft, 0, static_cast<FT_UInt>(std::lround(pixelHeight)));
			f.loadFlags = FT_LOAD_COLOR | FT_LOAD_TARGET_LIGHT;
		}
		else if (ft->num_fixed_sizes > 0)
		{
			// colour emoji: pick the smallest strike at least as big as asked, else the biggest
			int best = -1, biggest = 0;
			for (int s = 0; s < ft->num_fixed_sizes; s++)
			{
				const FT_Pos h = ft->available_sizes[s].y_ppem;
				if (h > ft->available_sizes[biggest].y_ppem) biggest = s;
				if (h / 64.0f >= pixelHeight && (best < 0 || h < ft->available_sizes[best].y_ppem)) best = s;
			}
			if (best < 0) best = biggest;
			FT_Select_Size(ft, best);
			f.bitmapOnly = true;
			// size->metrics.y_ppem is whole pixels (unlike available_sizes[].y_ppem, 26.6)
			f.scale = pixelHeight / static_cast<float>(ft->size->metrics.y_ppem);
			f.loadFlags = FT_LOAD_COLOR | FT_LOAD_DEFAULT;
		}
		f.hb = hb_ft_font_create_referenced(ft);
		hb_ft_font_set_load_flags(f.hb, f.loadFlags);
	}

	const Impl::Face& primary = m->faces.front();
	const FT_Size_Metrics& sm = primary.ft->size->metrics;
	m->asc = sm.ascender / 64.0f * primary.scale;
	m->desc = -sm.descender / 64.0f * primary.scale;
	m->lh = sm.height / 64.0f * primary.scale;
	m->buf = hb_buffer_create();
	m->resetAtlas(kFirstAtlas);
	impl_ = std::move(m);
}

void ShapedText::Delete()
{
	if (!impl_) return;
	if (impl_->dev && impl_->tex) impl_->dev->release(impl_->tex);
	impl_.reset();
}

bool ShapedText::ready() const { return impl_ != nullptr; }

ShapedText::Line ShapedText::shape(std::string_view utf8)
{
	if (!impl_) return {};
	bool overflow = false;
	Line line = impl_->shape(utf8, true, overflow);
	if (overflow)
	{
		// atlas full at its largest: start over with only this string's glyphs
		impl_->resetAtlas(impl_->size);
		overflow = false;
		line = impl_->shape(utf8, true, overflow);
	}
	return line;
}

float ShapedText::width(std::string_view utf8)
{
	if (!impl_) return 0.0f;
	bool overflow = false;
	return impl_->shape(utf8, false, overflow).advance;
}

float ShapedText::pixelHeight() const { return impl_ ? impl_->px : 0.0f; }
float ShapedText::ascent() const { return impl_ ? impl_->asc : 0.0f; }
float ShapedText::descent() const { return impl_ ? impl_->desc : 0.0f; }
float ShapedText::lineHeight() const { return impl_ ? impl_->lh : 0.0f; }
int ShapedText::fontFor(char32_t cp) const { return impl_ ? impl_->fontFor(cp) : -1; }
int ShapedText::fontCount() const { return impl_ ? static_cast<int>(impl_->faces.size()) : 0; }
int ShapedText::atlasSize() const { return impl_ ? impl_->size : 0; }

gpu::Texture ShapedText::texture(gpu::Device& dev)
{
	if (!impl_) return {};
	Impl& m = *impl_;
	if (m.tex && (m.dev != &dev || m.texSize != m.size))
	{
		m.dev->release(m.tex); // it grew (or moved device): a new one at the new size
		m.tex = {};
	}
	if (!m.tex)
	{
		m.dev = &dev;
		m.tex = dev.texture({ m.size, m.size, gpu::Format::RGBA8, gpu::TextureUsage::Sampled });
		m.texSize = m.size;
		m.dirty = true;
	}
	if (m.dirty)
	{
		dev.upload(m.tex, m.pixels.data(), 0, 0, m.size, m.size);
		m.dirty = false;
	}
	return m.tex;
}
