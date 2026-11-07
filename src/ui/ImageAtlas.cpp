#include "ui/ImageAtlas.h"

#include <algorithm>
#include <cstring>

namespace ui
{

namespace
{
constexpr int kPad = 1; // empty pixels around each image, so filtering never reaches a neighbour
}

ImageAtlas::ImageAtlas(int firstSize, int maxSize)
    : pixels_(static_cast<size_t>(firstSize) * firstSize * 4, 0), size_(firstSize), maxSize_(std::max(firstSize, maxSize))
{
}

ImageAtlas::~ImageAtlas() { release(); }

void ImageAtlas::release()
{
	if (dev_ && tex_) dev_->release(tex_);
	tex_ = {};
	dev_ = nullptr;
	texSize_ = 0;
	dirty_ = true;
}

void ImageAtlas::clear()
{
	std::fill(pixels_.begin(), pixels_.end(), 0);
	slots_.clear();
	shelfX_ = shelfY_ = kPad;
	shelfH_ = 0;
	dirty_ = true;
}

bool ImageAtlas::place(int w, int h, int& x, int& y)
{
	for (;;)
	{
		if (shelfX_ + w + kPad > size_) { shelfY_ += shelfH_ + kPad; shelfX_ = kPad; shelfH_ = 0; }
		if (w + 2 * kPad <= size_ && shelfY_ + h + kPad <= size_)
		{
			x = shelfX_;
			y = shelfY_;
			shelfX_ += w + kPad;
			shelfH_ = std::max(shelfH_, h);
			return true;
		}
		if (size_ >= maxSize_) return false;
		// double it, every image staying at its pixel position
		const int grown = std::min(size_ * 2, maxSize_);
		std::vector<uint8_t> next(static_cast<size_t>(grown) * grown * 4, 0);
		for (int row = 0; row < size_; row++)
			std::memcpy(&next[static_cast<size_t>(row) * grown * 4], &pixels_[static_cast<size_t>(row) * size_ * 4],
			            static_cast<size_t>(size_) * 4);
		pixels_.swap(next);
		// the shelf being filled may now go on to the right
		size_ = grown;
	}
}

int ImageAtlas::add(const Image& image)
{
	if (!image) return -1;
	int x = 0, y = 0;
	if (!place(image.w, image.h, x, y)) return -1;
	for (int row = 0; row < image.h; row++)
		std::memcpy(&pixels_[(static_cast<size_t>(y + row) * size_ + x) * 4], &image.pixels[static_cast<size_t>(row) * image.w * 4],
		            static_cast<size_t>(image.w) * 4);
	slots_.push_back({ x, y, image.w, image.h });
	dirty_ = true;
	return count() - 1;
}

glm::vec4 ImageAtlas::uv(int id) const
{
	const Slot& s = slots_[id];
	const float k = 1.0f / static_cast<float>(size_);
	return { s.x * k, s.y * k, (s.x + s.w) * k, (s.y + s.h) * k };
}

gpu::Texture ImageAtlas::texture(gpu::Device& dev)
{
	if (tex_ && (dev_ != &dev || texSize_ != size_))
	{
		dev_->release(tex_); // it grew (or moved device): a new one at the new size
		tex_ = {};
	}
	if (!tex_)
	{
		dev_ = &dev;
		tex_ = dev.texture({ size_, size_, gpu::Format::RGBA8, gpu::TextureUsage::Sampled });
		texSize_ = size_;
		dirty_ = true;
	}
	if (dirty_)
	{
		dev.upload(tex_, pixels_.data(), 0, 0, size_, size_);
		dirty_ = false;
	}
	return tex_;
}

}
