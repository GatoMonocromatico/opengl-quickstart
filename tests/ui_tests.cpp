// Tests for the GL-free parts of src/ui and src/util/Utf8.h.
// Plain asserts, one binary: build with run_tests.sh or the ui_tests CMake target.
#include "ui/Anim.h"
#include "ui/CanvasView.h"
#include "ui/Color.h"
#include "ui/Geometry.h"
#include "ui/TextEdit.h"
#include "ui/Wrap.h"
#include "util/Utf8.h"

#include <algorithm>
#include <cmath>
#include <functional>
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

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static void testEase()
{
	using ui::Ease;
	for (Ease e : { Ease::Linear, Ease::In, Ease::Out, Ease::InOut, Ease::Back })
	{
		CHECK(near(ui::ease(e, 0.0f), 0.0f));
		CHECK(near(ui::ease(e, 1.0f), 1.0f));
		CHECK(near(ui::ease(e, -1.0f), 0.0f) && near(ui::ease(e, 2.0f), 1.0f)); // clamped
	}
	CHECK(ui::ease(Ease::Out, 0.5f) > 0.5f);
	CHECK(ui::ease(Ease::In, 0.5f) < 0.5f);
	CHECK(near(ui::ease(Ease::InOut, 0.5f), 0.5f));
	float peak = 0;
	for (int i = 0; i <= 100; i++) peak = std::max(peak, ui::ease(Ease::Back, i / 100.0f));
	CHECK(peak > 1.03f && peak < 1.08f); // about 5% past
}

static void testAnimTimers()
{
	ui::Anim a;
	a.update(1000);
	CHECK(!a.moving() && a.waitTimeoutMs(1000) == -1);
	int done = 0;
	a.start("t", 100, [&] { done++; });
	CHECK(a.running("t") && near(a.progress("t"), 0.0f) && a.moving());
	// frames every frameMs while it runs
	CHECK(a.waitTimeoutMs(1000) == 33 && a.waitTimeoutMs(1040) == 0);
	a.update(1050);
	CHECK(near(a.progress("t"), 0.5f) && done == 0);
	a.update(1080);
	// its end comes before the next frame would: it gets a frame of its own
	CHECK(a.waitTimeoutMs(1080) == 20);
	a.update(1100);
	CHECK(!a.running("t") && near(a.progress("t"), 1.0f) && done == 1);
	a.update(1200);
	CHECK(done == 1 && !a.moving() && a.waitTimeoutMs(1200) == -1);
	CHECK(near(a.progress("never"), 1.0f) && !a.running("never"));

	// a chain: the second starts where the first ended, not at the frame that noticed
	a.start("a", 100, [&] { a.start("b", 100); });
	a.update(1330);
	CHECK(a.running("b") && near(a.progress("b"), 0.3f));

	// stopped: no done
	int stopped = 0;
	a.start("s", 50, [&] { stopped++; });
	a.stop("s");
	a.update(1400);
	CHECK(stopped == 0 && !a.running("s"));

	// after(): waits without frames
	ui::Anim w;
	w.update(0);
	int fired = 0;
	w.after(500, [&] { fired++; });
	CHECK(!w.moving() && w.waitTimeoutMs(100) == 400);
	w.update(499);
	CHECK(fired == 0);
	w.update(500);
	CHECK(fired == 1 && w.waitTimeoutMs(500) == -1);

	// reduced motion: a timer lands on the next update
	ui::Anim r;
	r.motionScale = 0.0f;
	r.update(0);
	int landed = 0;
	r.start("x", 400, [&] { landed++; });
	CHECK(!r.running("x") && near(r.progress("x"), 1.0f) && r.waitTimeoutMs(0) == 0);
	r.update(1);
	CHECK(landed == 1);
	// one that restarts itself at 0 ms fires once an update, it doesn't spin
	int loops = 0;
	std::function<void()> again = [&] { loops++; r.start("loop", 100, again); };
	r.start("loop", 100, again);
	r.update(2);
	r.update(3);
	CHECK(loops == 2);
}

static void testAnimTweens()
{
	ui::Anim a;
	a.update(0);
	// the first call starts on the target
	CHECK(near(a.tween("x", 10, 100), 10.0f) && !a.moving());
	CHECK(near(a.tween("x", 20, 100, ui::Ease::Linear), 10.0f) && a.moving());
	a.update(50);
	CHECK(near(a.tween("x", 20, 100, ui::Ease::Linear), 15.0f));
	// a new target half way starts from where it is
	CHECK(near(a.tween("x", 0, 100, ui::Ease::Linear), 15.0f));
	a.update(100);
	CHECK(near(a.tween("x", 0, 100, ui::Ease::Linear), 7.5f));
	a.update(150);
	CHECK(near(a.tween("x", 0, 100, ui::Ease::Linear), 0.0f) && !a.moving());
	a.set("x", 5);
	CHECK(near(a.tween("x", 5, 100), 5.0f) && !a.moving());
	// an index makes its own id
	CHECK(near(a.tween({ "card", 1 }, 1, 100), 1.0f) && near(a.tween({ "card", 2 }, 2, 100), 2.0f));
	CHECK(!(ui::AnimId("card", 1) == ui::AnimId("card", 2)) && ui::AnimId("card") == ui::AnimId(std::string_view("card")));
}

static void testAnimSprings()
{
	ui::Anim a;
	a.update(0);
	CHECK(near(a.spring("s", 0), 0.0f) && !a.moving());
	a.spring("s", 100);
	CHECK(a.moving());
	float peak = 0;
	uint64_t t = 0;
	while (a.moving() && t < 5000)
	{
		t += 16;
		a.update(t);
		peak = std::max(peak, a.spring("s", 100));
	}
	// about critically damped: it settles without a real overshoot
	CHECK(!a.moving() && near(a.spring("s", 100), 100.0f) && peak <= 100.5f);
	// lightly damped: past it, then back
	a.spring("u", 0, 220, 10);
	a.spring("u", 100, 220, 10);
	peak = 0;
	while (a.moving() && t < 20000)
	{
		t += 16;
		a.update(t);
		peak = std::max(peak, a.spring("u", 100, 220, 10));
	}
	CHECK(peak > 110.0f && near(a.spring("u", 100, 220, 10), 100.0f) && !a.moving());
}

static void testAnimRolls()
{
	ui::Anim a;
	a.update(0);
	int ticks = -1;
	CHECK(near(a.roll("n", 3), 3.0f) && !a.moving());
	// stepped: a unit every 40 ms, moving for 70% of it, then holding
	CHECK(near(a.roll("n", 6, {}, &ticks), 3.0f) && ticks == 0);
	a.update(14);
	CHECK(near(a.roll("n", 6, {}, &ticks), 3.5f) && ticks == 1); // half way rounds up: a tick
	a.update(35);
	CHECK(near(a.roll("n", 6, {}, &ticks), 4.0f) && ticks == 0); // holding
	a.update(80);
	CHECK(near(a.roll("n", 6, {}, &ticks), 5.0f) && ticks == 1);
	a.update(120);
	CHECK(near(a.roll("n", 6, {}, &ticks), 6.0f) && ticks == 1 && !a.moving());
	// a long way takes bigger steps, never longer than maxMs
	a.roll("n", 106);
	a.update(120 + 1600);
	CHECK(near(a.roll("n", 106), 106.0f) && !a.moving());
	// eased with Back: past the target, then on it
	const ui::Roll eased{ .stepMs = 70, .baseMs = 140, .maxMs = 900, .ease = ui::Ease::Back, .stepped = false };
	a.roll("m", 0, eased);
	a.roll("m", 10, eased); // 140 + 10 * 70 = 840 ms
	float peak = 0;
	for (uint64_t t = 1720; t <= 1720 + 840; t += 10)
	{
		a.update(t);
		peak = std::max(peak, a.roll("m", 10, eased));
	}
	CHECK(peak > 10.3f && near(a.roll("m", 10, eased), 10.0f) && !a.moving());
	// reduced motion: it's there at once
	ui::Anim r;
	r.motionScale = 0.0f;
	r.update(0);
	r.roll("n", 1);
	CHECK(near(r.roll("n", 50), 50.0f) && !r.moving());
}

static void testTransform()
{
	const ui::Transform2D outer{ { 10, 20 }, 2 }, inner{ { 3, 4 }, 0.5f };
	const glm::vec2 p{ 7, -3 };
	const glm::vec2 a = (outer * inner).apply(p), b = outer.apply(inner.apply(p));
	CHECK(near(a.x, b.x) && near(a.y, b.y));
	const glm::vec2 back = outer.inverse(outer.apply(p));
	CHECK(near(back.x, p.x) && near(back.y, p.y));
	const ui::Rect r = outer.apply(ui::Rect{ 1, 2, 3, 4 });
	CHECK(near(r.x, 12) && near(r.y, 24) && near(r.w, 6) && near(r.h, 8));
}

static void testCanvasView()
{
	const ui::Rect view{ 100, 50, 400, 300 };
	ui::CanvasView c;
	c.offset = { 20.4f, 10.6f };
	c.zoom = 1.5f;
	// world (0, 0) on a whole pixel; world and screen undo each other through it
	CHECK(near(c.origin(view).x, 120) && near(c.origin(view).y, 61));
	const glm::vec2 w{ 33, 44 };
	const glm::vec2 back = c.toWorld(c.toScreen(w, view), view);
	CHECK(near(back.x, w.x) && near(back.y, w.y));
	// zooming at a point keeps the world under it there (to the origin's rounding)
	const glm::vec2 at{ 260, 170 };
	const glm::vec2 before = c.toWorld(at, view);
	c.zoomAt(at, 1.12f, view);
	const glm::vec2 after = c.toWorld(at, view);
	CHECK(near(c.zoom, 1.68f) && std::fabs(after.x - before.x) < 0.5f && std::fabs(after.y - before.y) < 0.5f);
	c.zoomAt(at, 100.0f, view);
	CHECK(near(c.zoom, c.maxZoom));
	// fit: all of the world, centred, within the limits
	const ui::Rect world{ 0, 0, 800, 300 };
	c.fit(world, view, 10);
	CHECK(near(c.zoom, 0.5f)); // 380 / 800 is under minZoom
	c.minZoom = 0.25f;
	c.fit(world, view, 10);
	CHECK(near(c.zoom, 0.475f));
	const glm::vec2 mid = c.toScreen({ 400, 150 }, view);
	CHECK(std::fabs(mid.x - 300) <= 0.5f && std::fabs(mid.y - 200) <= 0.5f);
	const glm::vec2 centre = c.centered({ 400, 150 }, view);
	CHECK(near(centre.x, c.offset.x) && near(centre.y, c.offset.y));
}

static void testColor()
{
	const glm::vec4 c = ui::rgb(0x283a35);
	CHECK(near(c.r, 40 / 255.0f) && near(c.g, 58 / 255.0f) && near(c.b, 53 / 255.0f) && near(c.a, 1.0f));
	bool fits = false;
	// no chroma: L cubed is the linear grey, 0.4219, which encodes to 0.6814
	const glm::vec3 grey = ui::oklch(0.75f, 0.0f, 0.0f, &fits);
	CHECK(fits && near(grey.r, 0.6814f, 2e-3f) && near(grey.g, grey.r, 2e-3f) && near(grey.b, grey.r, 2e-3f));
	ui::oklch(0.7f, 0.4f, 145.0f, &fits);
	CHECK(!fits);
	const glm::vec3 white = ui::oklch(1.0f, 0.0f, 0.0f, &fits);
	CHECK(fits && near(white.r, 1.0f, 2e-3f));
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
	testEase();
	testAnimTimers();
	testAnimTweens();
	testAnimSprings();
	testAnimRolls();
	testTransform();
	testCanvasView();
	testColor();
	std::cout << gPassed << " passed, " << gFailed << " failed.\n";
	return gFailed == 0 ? 0 : 1;
}
