// Tests for the GL-free parts of src/ui and src/util/Utf8.h.
// Plain asserts, one binary: build with run_tests.sh or the ui_tests CMake target.
#include "ui/TextEdit.h"
#include "ui/Wrap.h"
#include "util/Utf8.h"

#include <iostream>
#include <string>

static int gPassed = 0, gFailed = 0;

#define CHECK(...) do { \
	if (__VA_ARGS__) gPassed++; \
	else { gFailed++; std::cout << "FAIL line " << __LINE__ << ": " #__VA_ARGS__ "\n"; } \
} while (0)

// every glyph 10 px wide, one codepoint = one glyph
static float fake(std::string_view s) { return 10.0f * static_cast<float>(utf8::count(s)); }

static std::vector<std::string> lineTexts(std::string_view t, float w)
{
	std::vector<std::string> out;
	for (const ui::LineRange& l : ui::wrap(t, w, fake)) out.emplace_back(t.substr(l.start, l.end - l.start));
	return out;
}

using V = std::vector<std::string>;

static void testUtf8()
{
	const std::string s = "a\xC3\xA7\xE2\x9C\x93z"; // a ç ✓ z
	CHECK(utf8::count(s) == 4);
	CHECK(utf8::next(s, 0) == 1);
	CHECK(utf8::next(s, 1) == 3);
	CHECK(utf8::next(s, 3) == 6);
	CHECK(utf8::next(s, 6) == 7);
	CHECK(utf8::next(s, 7) == 7);
	CHECK(utf8::prev(s, 7) == 6);
	CHECK(utf8::prev(s, 6) == 3);
	CHECK(utf8::prev(s, 3) == 1);
	CHECK(utf8::prev(s, 0) == 0);
	CHECK(utf8::snap(s, 2) == 1);
	CHECK(utf8::snap(s, 4) == 3);
	CHECK(utf8::snap(s, 5) == 3);
	size_t len = 0;
	CHECK(utf8::decode(s, 3, len) == 0x2713 && len == 3);
	CHECK(utf8::valid(s));

	// malformed input: lone continuation, truncated sequence, overlong, surrogate
	CHECK(!utf8::valid("\x80"));
	CHECK(!utf8::valid("ab\xE2\x9C"));
	CHECK(!utf8::valid("\xC0\x80"));
	CHECK(!utf8::valid("\xED\xA0\x80"));
	CHECK(utf8::sanitize("a\x80" "b") == "a\xEF\xBF\xBD" "b");
	CHECK(utf8::valid(utf8::sanitize("\xE2\x9C")));
	CHECK(utf8::count("\xE2\x9C") == 2); // every bad byte still advances
	std::string enc;
	utf8::append(enc, 0xE7);
	utf8::append(enc, 0x1F600);
	CHECK(enc == "\xC3\xA7\xF0\x9F\x98\x80");
	CHECK(utf8::next(enc, 2) == 6);
	CHECK(utf8::prev(enc, 6) == 2);
}

static void testWrap()
{
	CHECK(lineTexts("", 100) == V{ "" });
	CHECK(lineTexts("hello", 100) == V{ "hello" });
	CHECK(lineTexts("hello world", 60) == V{ "hello ", "world" });
	CHECK(lineTexts("aa bb cc dd", 50) == V{ "aa bb ", "cc dd" });
	// explicit newlines, empty lines and a trailing newline
	CHECK(lineTexts("a\n\nb\n", 100) == V{ "a", "", "b", "" });
	// long word hard-broken between codepoints
	CHECK(lineTexts("abcdefghij", 40) == V{ "abcd", "efgh", "ij" });
	CHECK(lineTexts("ab abcdefghij", 40) == V{ "ab ", "abcd", "efgh", "ij" });
	// narrower than one glyph still progresses, one codepoint per line
	CHECK(lineTexts("abc", 3) == V{ "a", "b", "c" });
	// multi-byte codepoints are never split
	CHECK(lineTexts("\xC3\xA7\xC3\xA7\xC3\xA7", 20) == V{ "\xC3\xA7\xC3\xA7", "\xC3\xA7" });
	// maxWidth <= 0 only breaks at newlines
	CHECK(lineTexts("a b c\nd", 0) == V{ "a b c", "d" });
	// ranges are contiguous offsets into the source
	const std::string t = "one two three four";
	auto lines = ui::wrap(t, 90, fake);
	CHECK(lines.front().start == 0 && lines.back().end == t.size());
	for (size_t i = 1; i < lines.size(); i++) CHECK(lines[i].start == lines[i - 1].end);

	CHECK(ui::lineOfOffset(lines, 0) == 0);
	CHECK(ui::lineOfOffset(lines, lines[1].start) == 1);
	CHECK(ui::lineOfOffset(lines, t.size()) == lines.size() - 1);

	// nearest boundary inside a line
	const ui::LineRange whole{ 0, 5 };
	CHECK(ui::indexInLine("hello", whole, -5, fake) == 0);
	CHECK(ui::indexInLine("hello", whole, 4, fake) == 0);
	CHECK(ui::indexInLine("hello", whole, 6, fake) == 1);
	CHECK(ui::indexInLine("hello", whole, 14, fake) == 1);
	CHECK(ui::indexInLine("hello", whole, 16, fake) == 2);
	CHECK(ui::indexInLine("hello", whole, 999, fake) == 5);
	// soft-wrapped line end is not a valid landing spot
	auto soft = ui::wrap("ab cd", 20, fake); // "ab " / "cd"
	CHECK(soft.size() == 2 && soft[0].end == soft[1].start);
	CHECK(ui::indexInLines("ab cd", soft, 0, 999, fake) == 2);
	CHECK(ui::indexInLines("ab cd", soft, 1, 999, fake) == 5);
}

static void testEditBasics()
{
	ui::TextEdit e;
	e.insert("hello");
	CHECK(e.text() == "hello" && e.caret() == 5 && !e.hasSelection());
	e.insert("\xC3\xA7"); // ç
	CHECK(e.text() == "hello\xC3\xA7" && e.caret() == 7);
	e.backspace(); // removes the whole codepoint
	CHECK(e.text() == "hello" && e.caret() == 5);
	e.left();
	e.left();
	e.insert("X");
	CHECK(e.text() == "helXlo" && e.caret() == 4);
	e.del();
	CHECK(e.text() == "helXo");
	e.home();
	CHECK(e.caret() == 0);
	e.backspace();
	CHECK(e.text() == "helXo"); // nothing before the caret
	e.end();
	e.del();
	CHECK(e.text() == "helXo");

	// invalid input is repaired, never stored
	ui::TextEdit b;
	b.insert("a\x80" "b");
	CHECK(utf8::valid(b.text()));
	b.setText("\xFF\xFE");
	CHECK(utf8::valid(b.text()));
}

static void testSelection()
{
	ui::TextEdit e("hello world");
	e.setCaret(0);
	e.right(true);
	e.right(true);
	CHECK(e.hasSelection() && e.selectedText() == "he");
	e.insert("J"); // replaces the selection
	CHECK(e.text() == "Jllo world" && e.caret() == 1 && !e.hasSelection());

	e.selectAll();
	CHECK(e.selectedText() == e.text());
	CHECK(e.cut() == "Jllo world");
	CHECK(e.text().empty() && e.caret() == 0);
	e.paste("abc");
	CHECK(e.text() == "abc");

	// plain left/right collapse a selection to its edge
	e.selectAll();
	e.left();
	CHECK(!e.hasSelection() && e.caret() == 0);
	e.selectAll();
	e.right();
	CHECK(!e.hasSelection() && e.caret() == 3);

	// backspace with a selection deletes just the selection
	ui::TextEdit s("abcdef");
	s.setCaret(1);
	s.setCaret(4, true);
	s.backspace();
	CHECK(s.text() == "aef" && s.caret() == 1);

	// selection backwards: anchor stays, caret moves left
	ui::TextEdit r("abcdef");
	r.left(true);
	r.left(true);
	CHECK(r.selectedText() == "ef" && r.caret() == 4 && r.anchor() == 6);

	// setCaret snaps inside a multi-byte codepoint
	ui::TextEdit u("\xC3\xA7z");
	u.setCaret(1);
	CHECK(u.caret() == 0);
}

static void testWords()
{
	ui::TextEdit e("foo bar  baz.qux");
	e.setCaret(e.text().size());
	e.left(false, true);
	CHECK(e.caret() == 13); // before "qux"
	e.left(false, true);
	CHECK(e.caret() == 12); // before "."
	e.left(false, true);
	CHECK(e.caret() == 9);  // before "baz"
	e.left(false, true);
	CHECK(e.caret() == 4);  // before "bar"
	e.left(false, true);
	CHECK(e.caret() == 0);
	e.right(false, true);
	CHECK(e.caret() == 4);  // start of the next word
	e.right(false, true);
	CHECK(e.caret() == 9);

	e.setText("one two");
	e.backspace(true);
	CHECK(e.text() == "one ");
	e.backspace(true);
	CHECK(e.text() == "");
	e.setText("one two");
	e.setCaret(0);
	e.del(true);
	CHECK(e.text() == "two");

	// accented letters are word characters
	ui::TextEdit a("a\xC3\xA7\xC3\xA3o b");
	a.setCaret(a.text().size());
	a.left(false, true);
	CHECK(a.caret() == 7); // before "b"
	a.left(false, true);
	CHECK(a.caret() == 0);

	ui::TextEdit pt("um par\xC3\xA1grafo longo");
	pt.selectWordAt(7);
	CHECK(pt.selectedText() == "par\xC3\xA1grafo");

	ui::TextEdit w("hello big world");
	w.selectWordAt(7);
	CHECK(w.selectedText() == "big");
	w.selectWordAt(w.text().size());
	CHECK(w.selectedText() == "world");
}

static void testLineKeys()
{
	ui::TextEdit e("ab\ncd\n\nef");
	e.setCaret(4);
	e.home();
	CHECK(e.caret() == 3);
	e.end();
	CHECK(e.caret() == 5);
	e.setCaret(6); // the empty line
	e.home();
	CHECK(e.caret() == 6);
	e.end();
	CHECK(e.caret() == 6);
	e.textStart(true);
	CHECK(e.selectedText() == "ab\ncd\n");
	e.textEnd();
	CHECK(e.caret() == e.text().size());
}

static void testVertical()
{
	// "abcdef gh" at 40 px: "abcd" "ef " "gh"? words: abcdef too long -> hard breaks
	ui::TextEdit e("one two\nthree four");
	auto lines = ui::wrap(e.text(), 1000, fake);
	CHECK(lines.size() == 2);
	e.setCaret(5); // "one t|wo"
	e.down(lines, fake);
	CHECK(e.caret() == 8 + 5); // same column on the second line
	e.up(lines, fake);
	CHECK(e.caret() == 5);
	e.up(lines, fake); // first line: jumps to the start
	CHECK(e.caret() == 0);
	e.setCaret(e.text().size());
	e.down(lines, fake); // last line: jumps to the end
	CHECK(e.caret() == e.text().size());

	// the column survives a short line in between
	ui::TextEdit g("abcdefgh\nab\nabcdefgh");
	auto gl = ui::wrap(g.text(), 1000, fake);
	g.setCaret(6);
	g.down(gl, fake);
	CHECK(g.caret() == 9 + 2); // end of "ab"
	g.down(gl, fake);
	CHECK(g.caret() == 12 + 6);
	g.up(gl, fake);
	g.up(gl, fake);
	CHECK(g.caret() == 6);

	// with soft wrap: "aa bb cc dd" at 50 px -> "aa bb " / "cc dd"
	ui::TextEdit w("aa bb cc dd");
	auto wl = ui::wrap(w.text(), 50, fake);
	CHECK(wl.size() == 2);
	w.setCaret(1);
	w.down(wl, fake);
	CHECK(w.caret() == wl[1].start + 1);
	w.up(wl, fake);
	CHECK(w.caret() == 1);
	// shift extends
	w.down(wl, fake, true);
	CHECK(w.hasSelection() && w.anchor() == 1);

	// multi-byte text moves by codepoint columns
	ui::TextEdit m("\xC3\xA7\xC3\xA3o\nxyz");
	auto ml = ui::wrap(m.text(), 1000, fake);
	m.setCaret(2 + 2); // after "çã"
	m.down(ml, fake);
	CHECK(m.caret() == 6 + 2);
}

int main()
{
	testUtf8();
	testWrap();
	testEditBasics();
	testSelection();
	testWords();
	testLineKeys();
	testVertical();
	std::cout << gPassed << " passed, " << gFailed << " failed.\n";
	return gFailed == 0 ? 0 : 1;
}
