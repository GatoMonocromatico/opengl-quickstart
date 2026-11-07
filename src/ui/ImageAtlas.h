#pragma once
// -----------------------------------------------------------------------------
// ImageAtlas.h -- many small images (icons) in one texture, so they draw in one batch
// -----------------------------------------------------------------------------
// add() packs an Image into a square RGBA atlas (premultiplied, shelf packing, one
// empty pixel around each) and returns its id. The atlas grows by doubling up to
// maxSize, keeping every image where it is; past that add() returns -1 and the
// owner decides what to drop (clear() and add again what's still in use). Packing
// needs no GPU; only texture(device) touches it, uploading what changed.
//
//   ui::ImageAtlas icons;
//   int kitty = icons.add(ui::loadImage(path, 32, 32));
//   painter.image(icons, kitty, 10, 10, 32, 32);

#include "gpu/Gpu.h"
#include "ui/Image.h"

#include <glm/glm.hpp>

#include <vector>

namespace ui
{

class ImageAtlas
{
public:
	struct Slot { int x = 0, y = 0, w = 0, h = 0; }; // atlas pixels

	explicit ImageAtlas(int firstSize = 256, int maxSize = 2048);
	~ImageAtlas();
	ImageAtlas(const ImageAtlas&) = delete;
	ImageAtlas& operator=(const ImageAtlas&) = delete;

	// -1 for an empty image or when it doesn't fit even at maxSize.
	int add(const Image& image);
	// Every id stops being valid; the atlas keeps its size.
	void clear();

	int count() const { return static_cast<int>(slots_.size()); }
	bool valid(int id) const { return id >= 0 && id < count(); }
	Slot slot(int id) const { return slots_[id]; }
	// Its rect in 0..1 (u0, v0, u1, v1) for the atlas as it is now: growing changes it.
	glm::vec4 uv(int id) const;
	int size() const { return size_; }

	// The atlas on device, uploading what changed first. A new texture when it grew
	// (the old one is released). The device must outlive the atlas, or call release().
	gpu::Texture texture(gpu::Device& device);
	// The texture as it is, without uploading, and whether texture() would change it.
	gpu::Texture current() const { return tex_; }
	bool dirty() const { return dirty_ || texSize_ != size_; }
	void release();

private:
	bool place(int w, int h, int& x, int& y);

	std::vector<uint8_t> pixels_;
	std::vector<Slot> slots_;
	int size_, maxSize_;
	int shelfX_ = 1, shelfY_ = 1, shelfH_ = 0;
	gpu::Device* dev_ = nullptr;
	gpu::Texture tex_;
	int texSize_ = 0;
	bool dirty_ = true;
};

}
