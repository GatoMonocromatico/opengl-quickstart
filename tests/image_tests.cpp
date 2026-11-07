// image_tests -- ui::Image (SVG/PNG loading, premultiplying, resampling) and
// ui::ImageAtlas packing. No GPU: the atlas is only packed, never uploaded.

#include "ui/Image.h"
#include "ui/ImageAtlas.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstdio>
#include <cstdlib>
#include <string>

static int passed = 0, failed = 0;
#define CHECK(cond)                                                          \
	do {                                                                     \
		if (cond) passed++;                                                  \
		else { failed++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
	} while (0)

static std::span<const uint8_t> bytes(const std::string& s) { return { reinterpret_cast<const uint8_t*>(s.data()), s.size() }; }
static const uint8_t* at(const ui::Image& im, int x, int y) { return &im.pixels[(static_cast<size_t>(y) * im.w + x) * 4]; }
static bool near(int a, int b, int tol = 2) { return std::abs(a - b) <= tol; }

// left half opaque red, right half 50% white
static const std::string kSvg =
    R"(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="8">)"
    R"(<rect x="0" y="0" width="8" height="8" fill="#ff0000"/>)"
    R"(<rect x="8" y="0" width="8" height="8" fill="#ffffff" fill-opacity="0.5"/></svg>)";

static void testSvg()
{
	ui::Image own = ui::loadImage(bytes(kSvg));
	CHECK(own.w == 16 && own.h == 8);

	ui::Image big = ui::loadImage(bytes(kSvg), 64, 32); // rasterised at that size, not scaled
	CHECK(big.w == 64 && big.h == 32);
	const uint8_t* red = at(big, 10, 10);
	CHECK(near(red[0], 255) && near(red[1], 0) && near(red[3], 255));
	const uint8_t* half = at(big, 50, 10); // premultiplied: 50% white is 128,128,128,128
	CHECK(near(half[0], 128) && near(half[3], 128));

	ui::Image aspect = ui::loadImage(bytes(kSvg), 32, 0);
	CHECK(aspect.w == 32 && aspect.h == 16);

	CHECK(!ui::loadImage(bytes("not an image")));
	CHECK(!ui::loadImage(std::string("/nonexistent/icon.svg"), 16, 16));
}

static void testPngAndResize(const std::string& dir)
{
	// a 4x4 PNG: opaque green, written through SDL_image, read back at 8x8 and 2x2
	SDL_Surface* s = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
	SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, 0, 255, 0, 255));
	const std::string path = dir + "image_tests_green.png";
	CHECK(IMG_SavePNG(s, path.c_str()));
	SDL_DestroySurface(s);
	ui::Image up = ui::loadImage(path, 8, 8), down = ui::loadImage(path, 2, 2), own = ui::loadImage(path);
	CHECK(own.w == 4 && up.w == 8 && up.h == 8 && down.w == 2);
	CHECK(near(at(up, 7, 7)[1], 255) && near(at(up, 7, 7)[3], 255));
	CHECK(near(at(down, 1, 1)[1], 255));
	SDL_RemovePath(path.c_str());

	// box filter: a 2x1 black|white averages to grey; straight alpha gets premultiplied
	const uint8_t bw[] = { 0, 0, 0, 255, 255, 255, 255, 255 };
	ui::Image one = ui::resized(ui::fromStraightRGBA(bw, 2, 1), 1, 1);
	CHECK(one.w == 1 && near(one.pixels[0], 128) && one.pixels[3] == 255);
	const uint8_t faint[] = { 200, 100, 0, 128 };
	ui::Image p = ui::fromStraightRGBA(faint, 1, 1);
	CHECK(near(p.pixels[0], 100) && near(p.pixels[1], 50) && p.pixels[3] == 128);
	// bilinear up keeps the ends exact
	ui::Image wide = ui::resized(ui::fromStraightRGBA(bw, 2, 1), 8, 1);
	CHECK(wide.pixels[0] == 0 && wide.pixels[7 * 4] == 255);
}

static ui::Image solid(int w, int h, uint8_t v)
{
	ui::Image im;
	im.w = w;
	im.h = h;
	im.pixels.assign(static_cast<size_t>(w) * h * 4, v);
	return im;
}

static void testAtlas()
{
	ui::ImageAtlas a(16, 64);
	CHECK(a.add(ui::Image{}) == -1);
	const int first = a.add(solid(6, 6, 10));
	const int second = a.add(solid(6, 6, 20));
	CHECK(first == 0 && second == 1 && a.size() == 16);
	const ui::ImageAtlas::Slot s0 = a.slot(first), s1 = a.slot(second);
	CHECK(s0.x == 1 && s0.y == 1 && s1.x == 8 && s1.y == 1); // one empty pixel between
	CHECK(a.dirty());

	const int big = a.add(solid(20, 20, 30)); // doesn't fit 16: grows, the others stay put
	CHECK(big == 2 && a.size() == 32);
	CHECK(a.slot(first).x == 1 && a.slot(second).x == 8);
	const glm::vec4 uv = a.uv(second);
	CHECK(uv.x == 8.0f / 32 && uv.z == 14.0f / 32);

	CHECK(a.add(solid(80, 80, 1)) == -1); // larger than the largest atlas
	for (int i = 0; i < 40; i++) a.add(solid(10, 10, 1));
	CHECK(a.size() == 64);
	CHECK(a.add(solid(30, 30, 1)) == -1); // full at maxSize: the owner clears
	a.clear();
	CHECK(a.count() == 0 && !a.valid(0));
	CHECK(a.add(solid(30, 30, 1)) == 0 && a.slot(0).x == 1);
}

int main()
{
	const char* base = SDL_GetBasePath();
	testSvg();
	testPngAndResize(base ? base : "./");
	testAtlas();
	std::printf("image_tests: %d passed, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
