#include "ui/Image.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>

namespace ui
{

namespace
{

// the size to aim for given the asked one and the image's own (0 = keep aspect / own)
void target(int ow, int oh, int& w, int& h)
{
	if (w <= 0 && h <= 0) { w = ow; h = oh; }
	else if (w <= 0) w = std::max(1, static_cast<int>(std::lround(static_cast<double>(ow) * h / oh)));
	else if (h <= 0) h = std::max(1, static_cast<int>(std::lround(static_cast<double>(oh) * w / ow)));
}

Image fromSurface(SDL_Surface* s, int w, int h)
{
	Image img;
	if (!s) return img;
	SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(s);
	if (!rgba) return img;
	img.w = rgba->w;
	img.h = rgba->h;
	img.pixels.resize(static_cast<size_t>(img.w) * img.h * 4);
	SDL_PremultiplyAlpha(img.w, img.h, SDL_PIXELFORMAT_RGBA32, rgba->pixels, rgba->pitch, SDL_PIXELFORMAT_RGBA32,
	                     img.pixels.data(), img.w * 4, false);
	SDL_DestroySurface(rgba);
	target(img.w, img.h, w, h);
	return (w == img.w && h == img.h) ? img : resized(img, w, h);
}

}

Image loadImage(std::span<const uint8_t> bytes, int w, int h)
{
	if (bytes.empty()) return {};
	SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
	if (!io) return {};
	if (IMG_isSVG(io) && (w > 0 || h > 0)) // rasterised at that size (a 0 side keeps the aspect)
	{
		Image img = fromSurface(IMG_LoadSizedSVG_IO(io, w, h), w, h);
		SDL_CloseIO(io);
		return img;
	}
	return fromSurface(IMG_Load_IO(io, true), w, h);
}

Image loadImage(const std::string& path, int w, int h)
{
	size_t size = 0;
	void* data = SDL_LoadFile(path.c_str(), &size);
	if (!data) return {};
	Image img = loadImage(std::span<const uint8_t>(static_cast<const uint8_t*>(data), size), w, h);
	SDL_free(data);
	return img;
}

Image fromStraightRGBA(const uint8_t* rgba, int w, int h)
{
	Image img;
	if (!rgba || w <= 0 || h <= 0) return img;
	img.w = w;
	img.h = h;
	img.pixels.resize(static_cast<size_t>(w) * h * 4);
	SDL_PremultiplyAlpha(w, h, SDL_PIXELFORMAT_RGBA32, rgba, w * 4, SDL_PIXELFORMAT_RGBA32, img.pixels.data(), w * 4, false);
	return img;
}

namespace
{

// One axis of a separable resample: for each destination pixel, the source pixels
// and their weights. Down: the box of source pixels it covers (partial ones count
// by how much). Up: the two nearest, bilinear.
struct Tap { int i; float w; };
std::vector<std::vector<Tap>> taps(int src, int dst)
{
	std::vector<std::vector<Tap>> t(dst);
	const double r = static_cast<double>(src) / dst;
	for (int d = 0; d < dst; d++)
	{
		if (r > 1.0)
		{
			const double a = d * r, b = (d + 1) * r;
			for (int s = static_cast<int>(a); s < src && s < b; s++)
			{
				const double cover = std::min<double>(s + 1, b) - std::max<double>(s, a);
				if (cover > 0) t[d].push_back({ s, static_cast<float>(cover / r) });
			}
		}
		else
		{
			const double c = std::clamp((d + 0.5) * r - 0.5, 0.0, static_cast<double>(src - 1));
			const int s0 = static_cast<int>(c), s1 = std::min(s0 + 1, src - 1);
			const float f = static_cast<float>(c - s0);
			t[d].push_back({ s0, 1.0f - f });
			if (s1 != s0) t[d].push_back({ s1, f });
		}
	}
	return t;
}

}

Image resized(const Image& src, int w, int h)
{
	if (!src || w <= 0 || h <= 0) return {};
	if (w == src.w && h == src.h) return src;
	const auto tx = taps(src.w, w), ty = taps(src.h, h);
	std::vector<float> rows(static_cast<size_t>(w) * src.h * 4); // horizontal pass
	for (int y = 0; y < src.h; y++)
		for (int x = 0; x < w; x++)
		{
			float* o = &rows[(static_cast<size_t>(y) * w + x) * 4];
			for (const Tap& t : tx[x])
				for (int c = 0; c < 4; c++) o[c] += t.w * src.pixels[(static_cast<size_t>(y) * src.w + t.i) * 4 + c];
		}
	Image out;
	out.w = w;
	out.h = h;
	out.pixels.resize(static_cast<size_t>(w) * h * 4);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			float acc[4] = { 0, 0, 0, 0 };
			for (const Tap& t : ty[y])
				for (int c = 0; c < 4; c++) acc[c] += t.w * rows[(static_cast<size_t>(t.i) * w + x) * 4 + c];
			for (int c = 0; c < 4; c++)
				out.pixels[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>(std::clamp(std::lround(acc[c]), 0L, 255L));
		}
	return out;
}

}
