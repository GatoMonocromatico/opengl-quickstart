#include "ui/Wrap.h"
#include "util/Utf8.h"

#include <algorithm>

namespace ui
{

// One paragraph (no '\n' inside) into lines.
static void wrapParagraph(std::string_view text, size_t ps, size_t pe, float maxWidth, const MeasureFn& measure, std::vector<LineRange>& out)
{
	if (maxWidth <= 0.0f || ps == pe)
	{
		out.push_back({ ps, pe });
		return;
	}

	size_t lineStart = ps;
	size_t accepted = ps; // end of the last word (plus its spaces) that fit on this line
	size_t pos = ps;
	while (pos < pe)
	{
		size_t wordEnd = pos;
		while (wordEnd < pe && text[wordEnd] != ' ') wordEnd++;
		size_t tokenEnd = wordEnd;
		while (tokenEnd < pe && text[tokenEnd] == ' ') tokenEnd++;

		if (measure(text.substr(lineStart, wordEnd - lineStart)) <= maxWidth)
		{
			accepted = tokenEnd;
			pos = tokenEnd;
			continue;
		}

		if (accepted > lineStart)
		{
			// the word goes to the next line
			out.push_back({ lineStart, accepted });
			lineStart = accepted;
			continue;
		}

		// the word alone is too wide: largest prefix that fits, at least one codepoint
		std::vector<size_t> cuts;
		for (size_t i = utf8::next(text, lineStart); i <= wordEnd; i = utf8::next(text, i))
		{
			cuts.push_back(i);
			if (i >= wordEnd) break;
		}
		size_t lo = 0, hi = cuts.size(); // number of cuts that fit, found by bisection
		while (lo < hi)
		{
			const size_t mid = (lo + hi) / 2;
			if (measure(text.substr(lineStart, cuts[mid] - lineStart)) <= maxWidth) lo = mid + 1;
			else hi = mid;
		}
		const size_t cut = lo == 0 ? cuts[0] : cuts[lo - 1];
		if (cut >= wordEnd)
		{
			// can't happen (the whole word didn't fit) unless measure isn't monotonic
			accepted = pos = tokenEnd;
			continue;
		}
		out.push_back({ lineStart, cut });
		lineStart = accepted = pos = cut;
	}
	out.push_back({ lineStart, pe });
}

std::vector<LineRange> wrap(std::string_view text, float maxWidth, const MeasureFn& measure)
{
	std::vector<LineRange> out;
	size_t ps = 0;
	while (true)
	{
		const size_t nl = text.find('\n', ps);
		const size_t pe = nl == std::string_view::npos ? text.size() : nl;
		wrapParagraph(text, ps, pe, maxWidth, measure, out);
		if (nl == std::string_view::npos) break;
		ps = nl + 1;
	}
	return out;
}

size_t lineOfOffset(const std::vector<LineRange>& lines, size_t offset)
{
	size_t lo = 0, hi = lines.size();
	while (lo < hi)
	{
		const size_t mid = (lo + hi) / 2;
		if (lines[mid].start <= offset) lo = mid + 1;
		else hi = mid;
	}
	return lo == 0 ? 0 : lo - 1;
}

size_t indexInLine(std::string_view text, const LineRange& line, float x, const MeasureFn& measure)
{
	if (x <= 0.0f) return line.start;
	size_t prevPos = line.start;
	float prevX = 0.0f;
	for (size_t i = utf8::next(text, line.start); ; i = utf8::next(text, i))
	{
		if (i > line.end) break;
		const float cx = measure(text.substr(line.start, i - line.start));
		if (x < (prevX + cx) * 0.5f) return prevPos;
		prevPos = i;
		prevX = cx;
		if (i >= line.end) break;
	}
	return prevPos;
}

size_t indexInLines(std::string_view text, const std::vector<LineRange>& lines, size_t i, float x, const MeasureFn& measure)
{
	const LineRange& line = lines[i];
	size_t to = indexInLine(text, line, x, measure);
	const bool soft = i + 1 < lines.size() && lines[i + 1].start == line.end;
	if (soft && to == line.end && to > line.start) to = utf8::prev(text, to);
	return to;
}

}
