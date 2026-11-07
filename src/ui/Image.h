#pragma once
// -----------------------------------------------------------------------------
// Image.h -- pictures and icons as premultiplied RGBA pixels, no GPU
// -----------------------------------------------------------------------------
// Anything SDL_image reads (PNG, JPEG, XPM, ...) plus SVG, which is rasterised at
// the size asked for instead of scaled, so an icon stays sharp at any size. The
// pixels are premultiplied (as ImageAtlas and colour glyphs keep them), so they
// filter and scale without dark or bright fringes. Put one in an ImageAtlas to draw
// it with Painter2D.
//
//   ui::Image icon = ui::loadImage("/usr/share/icons/.../kitty.svg", 48, 48);
//   ui::Image photo = ui::loadImage("wallpaper.png");          // its own size

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ui
{

struct Image
{
	int w = 0, h = 0;
	std::vector<uint8_t> pixels; // w * h RGBA, premultiplied, top row first
	explicit operator bool() const { return w > 0 && h > 0; }
};

// w x h is the size wanted; 0 for one of them keeps the aspect ratio, 0 for both the
// image's own size (an SVG's width/height). An SVG is rasterised at that size, other
// formats are resampled to it. An empty Image when it can't be read.
Image loadImage(const std::string& path, int w = 0, int h = 0);
Image loadImage(std::span<const uint8_t> bytes, int w = 0, int h = 0);

// Box filter going down, bilinear going up, per axis.
Image resized(const Image& src, int w, int h);

// From straight (not premultiplied) RGBA bytes.
Image fromStraightRGBA(const uint8_t* rgba, int w, int h);

}
