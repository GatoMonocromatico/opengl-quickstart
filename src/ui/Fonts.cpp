#include "ui/Fonts.h"

#include <cmath>

namespace ui
{

Text& Fonts::get(const std::string& path, float pixelHeight, Text::Charset charset)
{
	const int quarters = static_cast<int>(std::lround(pixelHeight * 4.0f));
	std::unique_ptr<Text>& slot = texts_[{ path, quarters, charset }];
	if (!slot)
	{
		// made aside, so a font that fails to load leaves nothing half made behind
		auto text = std::make_unique<Text>();
		text->create(path, static_cast<float>(quarters) / 4.0f, charset);
		slot = std::move(text);
	}
	return *slot;
}

void Fonts::Delete()
{
	for (auto& [key, text] : texts_)
		if (text) text->Delete();
	texts_.clear();
}

}
