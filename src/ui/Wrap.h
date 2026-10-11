#pragma once
// -----------------------------------------------------------------------------
// Wrap.h -- word wrapping over a measure function, no GL
// -----------------------------------------------------------------------------
// wrap() cuts a UTF-8 string into display lines. Ranges are byte offsets into the
// original text, so a caret offset maps straight onto a line. The measure function
// is what makes it testable: pass `[&](std::string_view s) { return text.width(s, scale); }`
// in the app, a fixed-width fake in a test.

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace ui
{

// [start, end) of one display line. A '\n' is never inside a range, it sits
// between two lines. A soft-wrapped line keeps its trailing spaces, so
// end == start of the next line there.
struct LineRange
{
	size_t start = 0;
	size_t end = 0;
};

using MeasureFn = std::function<float(std::string_view)>;

// Greedy word wrap. Explicit '\n' always breaks (empty lines are kept, and text
// ending in '\n' gets a last empty line). A word wider than maxWidth is broken
// between codepoints, at least one per line so it always progresses.
// maxWidth <= 0 only breaks at '\n'. Never returns an empty vector.
std::vector<LineRange> wrap(std::string_view text, float maxWidth, const MeasureFn& measure);

// Index of the line holding the caret offset: the last line starting at or
// before it, so a caret at a soft-wrap point belongs to the lower line.
size_t lineOfOffset(const std::vector<LineRange>& lines, size_t offset);

// Offset inside the line (on a codepoint boundary) nearest to x pixels from the
// line's left edge.
size_t indexInLine(std::string_view text, const LineRange& line, float x, const MeasureFn& measure);

// Same for lines[i] of a wrap() result, but never returns the end of a
// soft-wrapped line (that offset is drawn at the start of the next line), so a
// click or up/down past the end of a wrapped line stays on it.
size_t indexInLines(std::string_view text, const std::vector<LineRange>& lines, size_t i, float x, const MeasureFn& measure);

}
