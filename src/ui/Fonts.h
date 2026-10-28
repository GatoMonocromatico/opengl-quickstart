#pragma once
// -----------------------------------------------------------------------------
// Fonts.h -- Text atlases made the first time a face is asked for at a size
// -----------------------------------------------------------------------------
// An app with a few faces at a few sizes asks here instead of keeping a Text for
// each. Text under a zoom stays sharp when it's made at the size it's shown at:
//
//   Text& f = fonts.get(QS_FONT_DIR "VT323-Regular.ttf", std::round(12 * zoom));
//   painter.text(f, "Cache", x, y, ink, 1.0f / zoom);   // inside pushTransform(..., zoom)
//
// Needs the GL context, like Text. Every atlas is a texture of its own (its glyphs,
// a few hundred kB) kept till Delete(), so ask for whole pixel sizes rather than one
// per zoom step.

#include "render/Text.h"

#include <map>
#include <memory>
#include <string>
#include <tuple>

namespace ui
{

class Fonts
{
public:
	// pixelHeight is kept to a quarter of a pixel. Throws like Text::create. The
	// reference stays valid until Delete().
	Text& get(const std::string& path, float pixelHeight, Text::Charset charset = Text::Charset::Latin);
	void Delete();

private:
	using Key = std::tuple<std::string, int, Text::Charset>;
	std::map<Key, std::unique_ptr<Text>> texts_;
};

}
