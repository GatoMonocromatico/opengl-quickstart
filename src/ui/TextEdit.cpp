#include "ui/TextEdit.h"
#include "util/Utf8.h"

#include <algorithm>

namespace ui
{

// 0 = space, 1 = word character, 2 = punctuation
static int charClass(uint32_t cp)
{
	if (cp == ' ' || cp == '\t' || cp == '\n') return 0;
	if ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_') return 1;
	// letters beyond ASCII count as word characters, except the usual symbols
	if (cp >= 0x80 && !(cp >= 0x2010 && cp <= 0x2BFF) && cp != 0xA0) return 1;
	return 2;
}

void TextEdit::setText(std::string text)
{
	text_ = utf8::valid(text) ? std::move(text) : utf8::sanitize(text);
	caret_ = anchor_ = text_.size();
	goalX_ = -1.0f;
}

std::pair<size_t, size_t> TextEdit::selection() const
{
	return { std::min(caret_, anchor_), std::max(caret_, anchor_) };
}

std::string TextEdit::selectedText() const
{
	auto [a, b] = selection();
	return text_.substr(a, b - a);
}

void TextEdit::moveTo(size_t offset, bool extend, bool keepGoal)
{
	caret_ = utf8::snap(text_, std::min(offset, text_.size()));
	if (!extend) anchor_ = caret_;
	if (!keepGoal) goalX_ = -1.0f;
}

void TextEdit::setCaret(size_t offset, bool extend) { moveTo(offset, extend); }

void TextEdit::selectAll()
{
	anchor_ = 0;
	caret_ = text_.size();
	goalX_ = -1.0f;
}

void TextEdit::selectWordAt(size_t offset)
{
	offset = utf8::snap(text_, std::min(offset, text_.size()));
	size_t a = offset, b = offset;
	// pick the class of the codepoint under the offset, or before it at the end
	size_t probe = offset < text_.size() ? offset : utf8::prev(text_, offset);
	if (text_.empty()) return;
	size_t len;
	const int cls = charClass(utf8::decode(text_, probe, len));
	while (a > 0)
	{
		const size_t p = utf8::prev(text_, a);
		if (charClass(utf8::decode(text_, p, len)) != cls) break;
		a = p;
	}
	while (b < text_.size())
	{
		if (charClass(utf8::decode(text_, b, len)) != cls) break;
		b += len;
	}
	anchor_ = a;
	caret_ = b;
	goalX_ = -1.0f;
}

void TextEdit::insert(std::string_view s)
{
	const std::string clean = utf8::valid(s) ? std::string(s) : utf8::sanitize(s);
	auto [a, b] = selection();
	text_.replace(a, b - a, clean);
	caret_ = anchor_ = a + clean.size();
	goalX_ = -1.0f;
}

std::string TextEdit::cut()
{
	std::string out = selectedText();
	if (hasSelection()) insert("");
	return out;
}

size_t TextEdit::wordLeft(size_t from) const
{
	size_t i = from;
	size_t len;
	// spaces first, then the run of one class
	while (i > 0)
	{
		const size_t p = utf8::prev(text_, i);
		if (charClass(utf8::decode(text_, p, len)) != 0) break;
		i = p;
	}
	if (i == 0) return 0;
	const int cls = charClass(utf8::decode(text_, utf8::prev(text_, i), len));
	while (i > 0)
	{
		const size_t p = utf8::prev(text_, i);
		if (charClass(utf8::decode(text_, p, len)) != cls) break;
		i = p;
	}
	return i;
}

size_t TextEdit::wordRight(size_t from) const
{
	size_t i = from;
	size_t len;
	if (i >= text_.size()) return text_.size();
	const int cls = charClass(utf8::decode(text_, i, len));
	if (cls != 0)
	{
		while (i < text_.size() && charClass(utf8::decode(text_, i, len)) == cls) i += len;
	}
	while (i < text_.size() && charClass(utf8::decode(text_, i, len)) == 0) i += len;
	return i;
}

void TextEdit::backspace(bool byWord)
{
	if (hasSelection()) { insert(""); return; }
	if (caret_ == 0) return;
	const size_t to = byWord ? wordLeft(caret_) : utf8::prev(text_, caret_);
	text_.erase(to, caret_ - to);
	caret_ = anchor_ = to;
	goalX_ = -1.0f;
}

void TextEdit::del(bool byWord)
{
	if (hasSelection()) { insert(""); return; }
	if (caret_ >= text_.size()) return;
	const size_t to = byWord ? wordRight(caret_) : utf8::next(text_, caret_);
	text_.erase(caret_, to - caret_);
	anchor_ = caret_;
	goalX_ = -1.0f;
}

void TextEdit::left(bool extend, bool byWord)
{
	if (!extend && hasSelection()) { moveTo(selection().first, false); return; }
	moveTo(byWord ? wordLeft(caret_) : utf8::prev(text_, caret_), extend);
}

void TextEdit::right(bool extend, bool byWord)
{
	if (!extend && hasSelection()) { moveTo(selection().second, false); return; }
	moveTo(byWord ? wordRight(caret_) : utf8::next(text_, caret_), extend);
}

void TextEdit::home(bool extend)
{
	const size_t nl = caret_ == 0 ? std::string::npos : text_.rfind('\n', caret_ - 1);
	moveTo(nl == std::string::npos ? 0 : nl + 1, extend);
}

void TextEdit::end(bool extend)
{
	const size_t nl = text_.find('\n', caret_);
	moveTo(nl == std::string::npos ? text_.size() : nl, extend);
}

void TextEdit::textStart(bool extend) { moveTo(0, extend); }
void TextEdit::textEnd(bool extend) { moveTo(text_.size(), extend); }

void TextEdit::vertical(int dir, const std::vector<LineRange>& lines, const MeasureFn& measure, bool extend)
{
	if (lines.empty()) return;
	const size_t cur = lineOfOffset(lines, caret_);
	const LineRange& line = lines[cur];
	if (goalX_ < 0.0f)
		goalX_ = measure(std::string_view(text_).substr(line.start, std::min(caret_, line.end) - line.start));

	if ((dir < 0 && cur == 0) || (dir > 0 && cur + 1 >= lines.size()))
	{
		// first / last line: jump to the text edge like most editors
		moveTo(dir < 0 ? 0 : text_.size(), extend);
		return;
	}
	const float goal = goalX_;
	const size_t to = indexInLines(text_, lines, cur + dir, goal, measure);
	moveTo(to, extend, true);
	goalX_ = goal;
}

void TextEdit::up(const std::vector<LineRange>& lines, const MeasureFn& measure, bool extend)
{
	vertical(-1, lines, measure, extend);
}

void TextEdit::down(const std::vector<LineRange>& lines, const MeasureFn& measure, bool extend)
{
	vertical(1, lines, measure, extend);
}

}
