#pragma once
// -----------------------------------------------------------------------------
// TextEdit.h -- editable UTF-8 string with caret and selection, no GL
// -----------------------------------------------------------------------------
// The state of a text box without the box: a string, a caret and a selection
// anchor, all byte offsets that always sit on codepoint boundaries. Feed it what
// the keyboard does; draw it however you like (ui::Context::textField does, a
// custom canvas can too). Clipboard access lives in the caller, which passes the
// text in and out. No undo: the app owns that.

#include "ui/Wrap.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui
{

class TextEdit
{
public:
	TextEdit() = default;
	explicit TextEdit(std::string text) { setText(std::move(text)); }

	const std::string& text() const { return text_; }
	// Replaces everything; caret goes to the end, selection is dropped.
	void setText(std::string text);

	size_t caret() const { return caret_; }
	size_t anchor() const { return anchor_; }
	bool hasSelection() const { return caret_ != anchor_; }
	// [begin, end) of the selection, or an empty range at the caret
	std::pair<size_t, size_t> selection() const;
	std::string selectedText() const;

	// Moves the caret (snapped to a boundary). extend keeps the anchor, so the
	// range between them is selected.
	void setCaret(size_t offset, bool extend = false);
	void selectAll();
	// selects the word under offset (double-click)
	void selectWordAt(size_t offset);

	// Replaces the selection (if any) with s and puts the caret after it. Malformed
	// UTF-8 in s is replaced with U+FFFD.
	void insert(std::string_view s);
	// Same as insert; named for the clipboard side of the caller.
	void paste(std::string_view s) { insert(s); }
	// Removes and returns the selection (empty string when none).
	std::string cut();

	// Deletes the selection, else one codepoint (or one word) before / after the caret.
	void backspace(bool byWord = false);
	void del(bool byWord = false);

	// extend = Shift held. Without extend a selection collapses to its edge first.
	void left(bool extend = false, bool byWord = false);
	void right(bool extend = false, bool byWord = false);
	void home(bool extend = false);        // start of the line (up to the previous '\n')
	void end(bool extend = false);         // end of the line
	void textStart(bool extend = false);
	void textEnd(bool extend = false);

	// Up / down over the display lines in `lines` (from wrap() on text() with the
	// same measure). Remembers the column across consecutive calls, like editors do.
	void up(const std::vector<LineRange>& lines, const MeasureFn& measure, bool extend = false);
	void down(const std::vector<LineRange>& lines, const MeasureFn& measure, bool extend = false);

private:
	void moveTo(size_t offset, bool extend, bool keepGoal = false);
	void vertical(int dir, const std::vector<LineRange>& lines, const MeasureFn& measure, bool extend);
	size_t wordLeft(size_t from) const;
	size_t wordRight(size_t from) const;

	std::string text_;
	size_t caret_ = 0;
	size_t anchor_ = 0;
	float goalX_ = -1.0f; // column kept by up/down, < 0 when unset
};

}
