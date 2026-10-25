#include "ui/Context.h"
#include "render/Text.h"
#include "util/Utf8.h"

#include <algorithm>
#include <cmath>

namespace ui
{

static constexpr uint64_t kBlinkMs = 530;
static constexpr float kScrollbarW = 6.0f;

void Context::init(Painter2D& painter, Text& font)
{
	painter_ = &painter;
	font_ = baseFont_ = &font;
}

// --- input ---------------------------------------------------------------

bool Context::handleEvent(const SDL_Event& e)
{
	auto densityOf = [&](SDL_WindowID id) {
		if (SDL_Window* w = SDL_GetWindowFromID(id)) density_ = SDL_GetWindowPixelDensity(w);
		if (density_ <= 0.0f) density_ = 1.0f;
	};
	const uint64_t now = SDL_GetTicks();

	switch (e.type)
	{
	case SDL_EVENT_MOUSE_MOTION:
		densityOf(e.motion.windowID);
		mouse_ = { e.motion.x * density_, e.motion.y * density_ };
		if (down_) pending_.drag += glm::vec2(e.motion.xrel, e.motion.yrel) * density_;
		dirty_ = true;
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		densityOf(e.button.windowID);
		mouse_ = { e.button.x * density_, e.button.y * density_ };
		down_ |= 1u << e.button.button;
		pending_.pressed |= 1u << e.button.button;
		pending_.clicks = e.button.clicks;
		lastInputMs_ = now;
		dirty_ = true;
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		densityOf(e.button.windowID);
		mouse_ = { e.button.x * density_, e.button.y * density_ };
		down_ &= ~(1u << e.button.button);
		pending_.released |= 1u << e.button.button;
		dirty_ = true;
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		pending_.wheel += e.wheel.y;
		dirty_ = true;
		break;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		mouse_ = { -1, -1 };
		dirty_ = true;
		break;
	case SDL_EVENT_KEY_DOWN:
		pending_.events.push_back({ false, {}, e.key.key, e.key.mod });
		lastInputMs_ = now;
		dirty_ = true;
		return textFocused_;
	case SDL_EVENT_TEXT_INPUT:
		pending_.events.push_back({ true, e.text.text ? e.text.text : "", 0, 0 });
		lastInputMs_ = now;
		dirty_ = true;
		return textFocused_;
	case SDL_EVENT_KEY_UP:
		return textFocused_;
	default:
		// resize, expose, focus changes... anything that can need a repaint
		if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) dirty_ = true;
		break;
	}
	return false;
}

void Context::beginFrame(int width, int height)
{
	w_ = width;
	h_ = height;
	in_ = std::move(pending_);
	pending_ = Input{};
	// cleared here, not at the end: a redraw asked for during the frame (a button's handler) gets another one
	dirty_ = false;
	// after that, so what a finished animation's callback asks for still counts
	lastFrameMs_ = SDL_GetTicks();
	anim.motionScale = theme.motionScale;
	anim.update(lastFrameMs_);
	dragDelta_ = in_.drag;
	wheel_ = in_.wheel;
	submittedId_ = 0;
	pressClaimed_ = false;
	textFocused_ = false;
	focusLost_ = false;
	clips_.clear();
	scrollStack_.clear();
	canvasStack_.clear();
	idStack_.clear();
	fontStack_.clear();
	font_ = baseFont_;
	disabled_ = 0;
	painter_->begin(width, height);
}

void Context::endFrame()
{
	painter_->end();
	in_.pressed &= 1u << SDL_BUTTON_LEFT;
	const bool lostFocus = focusLost_ || (in_.pressed && !pressClaimed_ && focus_ != 0);
	if (!(down_ & (1u << SDL_BUTTON_LEFT))) active_ = 0;
	in_ = Input{};
	drawnPhase_ = blinkPhase();
	// a click that hit no widget takes the focus away; repaint without the ring
	if (lostFocus)
	{
		focus_ = 0;
		textFocused_ = false;
		dirty_ = true;
	}
}

int Context::blinkPhase() const
{
	return static_cast<int>(((SDL_GetTicks() - lastInputMs_) / kBlinkMs) % 2);
}

// ms until the frame cap lets the next frame through
int Context::capWaitMs() const
{
	const uint64_t now = SDL_GetTicks(), next = lastFrameMs_ + static_cast<uint64_t>(std::max(minFrameMs, 0));
	return next > now ? static_cast<int>(next - now) : 0;
}

bool Context::needsRedraw() const
{
	const bool due = dirty_ || anim.waitTimeoutMs(SDL_GetTicks()) == 0 || (textFocused_ && blinkPhase() != drawnPhase_);
	return due && capWaitMs() == 0;
}

int Context::waitTimeoutMs() const
{
	if (dirty_) return capWaitMs();
	int wait = anim.waitTimeoutMs(SDL_GetTicks());
	if (textFocused_)
	{
		const uint64_t into = (SDL_GetTicks() - lastInputMs_) % kBlinkMs;
		const int blink = static_cast<int>(kBlinkMs - into);
		wait = wait < 0 ? blink : std::min(wait, blink);
	}
	return wait < 0 ? wait : std::max(wait, capWaitMs());
}

// --- mouse helpers ---------------------------------------------------------

glm::vec2 Context::mousePos() const { return painter_->transform().inverse(mouse_); }

bool Context::hovered(const Rect& r) const
{
	if (disabled_ > 0 || !painter_->transform().apply(r).contains(mouse_)) return false;
	return clips_.empty() || clips_.back().contains(mouse_);
}

bool Context::pressed(const Rect& r, int button) const
{
	return (in_.pressed & (1u << button)) && hovered(r);
}

bool Context::released(const Rect& r, int button) const
{
	return (in_.released & (1u << button)) && hovered(r);
}

bool Context::doubleClicked(const Rect& r) const
{
	return pressed(r) && in_.clicks == 2;
}

// --- ids, focus, clip --------------------------------------------------------

std::string_view Context::shown(std::string_view label)
{
	const size_t k = label.find("##");
	return k == std::string_view::npos ? label : label.substr(0, k);
}

Context::Id Context::idOf(std::string_view label) const
{
	uint32_t h = idStack_.empty() ? 2166136261u : idStack_.back();
	for (unsigned char c : label) h = (h ^ c) * 16777619u;
	return h ? h : 1;
}

void Context::pushId(std::string_view id) { idStack_.push_back(idOf(id)); }
void Context::popId() { if (!idStack_.empty()) idStack_.pop_back(); }

void Context::setFocus(std::string_view id)
{
	focus_ = idOf(id);
	lastInputMs_ = SDL_GetTicks();
	dirty_ = true;
}

bool Context::submitted(std::string_view id) const { return submittedId_ == idOf(id); }

void Context::pushClip(const Rect& r)
{
	Rect c = painter_->transform().apply(r);
	if (!clips_.empty())
	{
		const Rect& p = clips_.back();
		const float x0 = std::max(c.x, p.x), y0 = std::max(c.y, p.y);
		const float x1 = std::min(c.x + c.w, p.x + p.w), y1 = std::min(c.y + c.h, p.y + p.h);
		c = { x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0) };
	}
	clips_.push_back(c);
	painter_->pushClip(r.x, r.y, r.w, r.h);
}

void Context::popClip()
{
	if (clips_.empty()) return;
	clips_.pop_back();
	painter_->popClip();
}

// the painter's transform is the one hit-testing reads, so they can't drift apart
void Context::pushTransform(glm::vec2 offset, float scale) { painter_->pushTransform(offset, scale); }
void Context::popTransform() { painter_->popTransform(); }

void Context::pushDisabled() { disabled_++; }
void Context::popDisabled() { if (disabled_ > 0) disabled_--; }

void Context::pushFont(Text& font)
{
	fontStack_.push_back(font_);
	font_ = &font;
}

void Context::popFont()
{
	if (fontStack_.empty()) return;
	font_ = fontStack_.back();
	fontStack_.pop_back();
}

// --- text helpers -------------------------------------------------------------

float Context::textWidth(std::string_view s) const { return font_->width(s, theme.fontScale); }
float Context::lineHeight() const { return font_->lineHeight() * theme.fontScale; }

MeasureFn Context::measure() const
{
	return [this](std::string_view s) { return font_->width(s, theme.fontScale); };
}

void Context::drawText(std::string_view s, float x, float baseline, const glm::vec4& color)
{
	painter_->text(*font_, s, std::round(x), std::round(baseline), color, theme.fontScale);
}

void Context::drawTextIn(const Rect& r, std::string_view s, Align align, const glm::vec4& color)
{
	const float asc = font_->ascent(theme.fontScale), desc = font_->descent(theme.fontScale);
	const float base = r.y + (r.h - (asc + desc)) * 0.5f + asc;
	const float tw = textWidth(s);
	float x = r.x;
	if (align == Align::Center) x = r.x + (r.w - tw) * 0.5f;
	else if (align == Align::Right) x = r.x + r.w - tw;
	// wider than the rect: keep the start visible
	if (tw > r.w) x = r.x;
	pushClip(r);
	drawText(s, x, base, color);
	popClip();
}

void Context::label(float x, float y, std::string_view s, std::optional<glm::vec4> color)
{
	drawText(shown(s), x, y + font_->ascent(theme.fontScale), color.value_or(theme.text));
}

void Context::label(const Rect& r, std::string_view s, Align align, std::optional<glm::vec4> color)
{
	drawTextIn(r, shown(s), align, color.value_or(theme.text));
}

// --- widgets ----------------------------------------------------------------

bool Context::canHover(Id id, const Rect& r) const
{
	return hovered(r) && (active_ == 0 || active_ == id);
}

// press then release over the region = click. hot is true while the mouse is
// over it (and nothing else is being dragged).
bool Context::clickRegion(const Rect& r, Id id, bool& hot)
{
	hot = canHover(id, r);
	bool clicked = false;
	const unsigned left = 1u << SDL_BUTTON_LEFT;
	if (hot && (in_.pressed & left))
	{
		active_ = id;
		pressClaimed_ = true;
		if (focus_ != 0) { focus_ = 0; focusLost_ = true; }
	}
	if (active_ == id && (in_.released & left))
	{
		clicked = hovered(r);
		active_ = 0;
	}
	return clicked;
}

bool Context::button(const Rect& r, std::string_view label)
{
	const Id id = idOf(label);
	bool hot;
	const bool clicked = clickRegion(r, id, hot);
	const glm::vec4& fill = active_ == id && hot ? theme.buttonActive : hot ? theme.buttonHover : theme.button;
	painter_->roundedRect(r.x, r.y, r.w, r.h, theme.radius, fill);
	drawTextIn(r.inset(theme.padding * 0.5f), shown(label), Align::Center, theme.text);
	return clicked;
}

bool Context::checkbox(const Rect& r, std::string_view label, bool& value)
{
	const Id id = idOf(label);
	bool hot;
	const bool clicked = clickRegion(r, id, hot);
	if (clicked) value = !value;

	const float box = std::min(r.h, lineHeight() * 0.9f);
	const float bx = r.x, by = r.y + (r.h - box) * 0.5f;
	if (value)
	{
		painter_->roundedRect(bx, by, box, box, theme.radius * 0.6f, theme.accent);
		const float t = std::max(2.0f, box * 0.12f);
		painter_->line(bx + box * 0.24f, by + box * 0.54f, bx + box * 0.43f, by + box * 0.74f, t, theme.accentText);
		painter_->line(bx + box * 0.43f, by + box * 0.74f, bx + box * 0.78f, by + box * 0.28f, t, theme.accentText);
	}
	else
	{
		painter_->roundedRect(bx, by, box, box, theme.radius * 0.6f, hot ? theme.buttonHover : theme.field);
		painter_->outline(bx, by, box, box, theme.borderWidth, theme.border, theme.radius * 0.6f);
	}
	const float gap = theme.padding;
	drawTextIn({ bx + box + gap, r.y, r.w - box - gap, r.h }, shown(label), Align::Left, theme.text);
	return clicked;
}

void Context::progressBar(const Rect& r, float fraction, std::string_view label)
{
	fraction = std::clamp(fraction, 0.0f, 1.0f);
	painter_->roundedRect(r.x, r.y, r.w, r.h, theme.radius, theme.field);
	if (fraction > 0.0f)
	{
		painter_->roundedRect(r.x, r.y, fraction * r.w, r.h, theme.radius, theme.accent);
	}
	painter_->outline(r.x, r.y, r.w, r.h, theme.borderWidth, theme.border, theme.radius);
	if (!label.empty()) drawTextIn(r, shown(label), Align::Center, theme.text);
}

TabResult Context::tabBar(const Rect& r, const std::vector<std::string>& labels, int active, bool closable)
{
	TabResult result;
	if (labels.empty()) return result;
	painter_->rect(r.x, r.y, r.w, r.h, theme.panel);

	const float tabW = std::min(200.0f, r.w / static_cast<float>(labels.size()));
	const float closeW = closable ? r.h * 0.7f : 0.0f;
	for (size_t i = 0; i < labels.size(); i++)
	{
		pushId(std::to_string(i));
		const Rect tab{ r.x + tabW * static_cast<float>(i), r.y, tabW, r.h };
		const Rect closeBtn{ tab.x + tab.w - closeW - 4, tab.y + (tab.h - closeW) * 0.5f, closeW, closeW };

		bool closeHot = false;
		if (closable)
		{
			if (clickRegion(closeBtn, idOf("x"), closeHot)) result.closed = static_cast<int>(i);
		}
		bool hot;
		if (clickRegion(tab, idOf("tab"), hot) && result.closed < 0) result.clicked = static_cast<int>(i);
		hot = hot && !closeHot;

		const bool isActive = static_cast<int>(i) == active;
		const glm::vec4 fill = isActive ? theme.background : hot ? theme.buttonHover : theme.panel;
		painter_->rect(tab.x, tab.y, tab.w, tab.h, fill);
		if (isActive) painter_->rect(tab.x, tab.y, tab.w, 2, theme.accent);
		painter_->rect(tab.x + tab.w - 1, tab.y + 4, 1, tab.h - 8, theme.border);

		const Rect text{ tab.x + theme.padding, tab.y, tab.w - theme.padding * 2 - closeW, tab.h };
		drawTextIn(text, labels[i], Align::Left, isActive ? theme.text : theme.textDim);
		if (closable)
		{
			if (closeHot) painter_->roundedRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, theme.radius, theme.buttonActive);
			const float c = closeW * 0.28f, cx = closeBtn.x + closeW * 0.5f, cy = closeBtn.y + closeW * 0.5f;
			const glm::vec4 xc = closeHot ? theme.text : theme.textDim;
			painter_->line(cx - c, cy - c, cx + c, cy + c, 1.6f, xc);
			painter_->line(cx - c, cy + c, cx + c, cy - c, 1.6f, xc);
		}
		popId();
	}
	return result;
}

// --- scroll area ---------------------------------------------------------------

float Context::beginScroll(const Rect& view, std::string_view id, float contentHeight)
{
	const Id sid = idOf(id);
	float& off = scroll_[sid];
	off = std::clamp(off, 0.0f, std::max(0.0f, contentHeight - view.h));
	scrollStack_.push_back({ view, sid, contentHeight });
	pushClip(view);
	return off;
}

void Context::endScroll()
{
	if (scrollStack_.empty()) return;
	const ScrollFrame f = scrollStack_.back();
	scrollStack_.pop_back();
	popClip();

	float& off = scroll_[f.id];
	const float drawnAt = off;
	const float maxOff = std::max(0.0f, f.contentH - f.view.h);
	if (maxOff <= 0.0f) { off = 0.0f; return; }

	// children (a text field) take the wheel first; what is left scrolls this area
	if (hovered(f.view) && wheel_ != 0.0f)
	{
		off -= wheel_ * lineHeight() * 3.0f;
		wheel_ = 0.0f;
	}

	const float trackH = f.view.h;
	const float thumbH = std::clamp(trackH * f.view.h / f.contentH, 24.0f, trackH);
	const Rect track{ f.view.x + f.view.w - kScrollbarW - 2, f.view.y, kScrollbarW + 2, trackH };
	const Id thumbId = f.id ^ 0x9E3779B9u;
	const float travel = trackH - thumbH;
	Rect thumb{ track.x + 1, f.view.y + (travel > 0 ? off / maxOff * travel : 0), kScrollbarW, thumbH };

	bool hot;
	clickRegion(thumb, thumbId, hot);
	if (active_ == thumbId && travel > 0.0f) off += dragDelta().y * maxOff / travel;
	off = std::clamp(off, 0.0f, maxOff);
	// the content went out at the old offset: one more frame shows where it scrolled to
	if (off != drawnAt) dirty_ = true;
	thumb.y = f.view.y + off / maxOff * travel;
	painter_->roundedRect(thumb.x, thumb.y, thumb.w, thumb.h, kScrollbarW * 0.5f,
		hot || active_ == thumbId ? theme.scrollThumb : glm::vec4(glm::vec3(theme.scrollThumb), theme.scrollThumb.a * 0.6f));
}

// --- canvas ------------------------------------------------------------------------

static AnimId flyId(uint32_t canvas) { return AnimId("ui.canvas.fly", static_cast<int>(canvas)); }

CanvasInput Context::beginCanvas(const Rect& view, std::string_view id)
{
	const Id cid = idOf(id);
	CanvasState& c = canvases_[cid];
	c.view = view;
	CanvasInput out;

	if (c.flying)
	{
		c.cam.offset = glm::mix(c.flyFrom, c.flyTo, ease(c.flyEase, anim.progress(flyId(cid))));
		c.flying = anim.running(flyId(cid));
	}
	// the press it took in endCanvas: a pan once it moves past the threshold, else a click
	const glm::vec2 m = mousePos();
	const unsigned left = 1u << SDL_BUTTON_LEFT;
	if (c.clickNext)
	{
		out.clicked = true;
		out.world = c.cam.toWorld(c.pressAt, view);
		c.clickNext = false;
	}
	else if (active_ == cid)
	{
		if (!c.moved && glm::length(m - c.pressAt) > c.cam.dragThreshold) c.moved = true;
		if (c.moved)
		{
			c.cam.offset = c.offsetAtPress + (m - c.pressAt);
			c.flying = false;
			out.panning = true;
		}
		if ((in_.released & left) && !c.moved)
		{
			out.clicked = true;
			out.world = c.cam.toWorld(m, view);
		}
	}
	if (wheel_ != 0.0f && hovered(view))
	{
		c.cam.zoomAt(m, std::pow(c.cam.wheelStep, wheel_), view);
		c.flying = false;
		wheel_ = 0.0f;
	}

	pushClip(view);
	pushTransform(c.cam.origin(view), c.cam.zoom);
	canvasStack_.push_back(cid);
	return out;
}

void Context::endCanvas()
{
	if (canvasStack_.empty()) return;
	const Id cid = canvasStack_.back();
	canvasStack_.pop_back();
	popTransform();
	popClip();

	// a press on the view that no widget inside took is the canvas's
	CanvasState& c = canvases_[cid];
	const unsigned left = 1u << SDL_BUTTON_LEFT;
	if ((in_.pressed & left) && !pressClaimed_ && active_ == 0 && hovered(c.view))
	{
		active_ = cid;
		pressClaimed_ = true;
		if (focus_ != 0) { focus_ = 0; focusLost_ = true; }
		c.pressAt = mousePos();
		c.offsetAtPress = c.cam.offset;
		c.moved = false;
		// already let go (a quick click inside one frame): it's a click, told next frame
		if ((in_.released & left) && !(down_ & left))
		{
			c.clickNext = true;
			dirty_ = true;
		}
	}
}

CanvasView& Context::canvas(std::string_view id) { return canvases_[idOf(id)].cam; }

void Context::flyCanvas(std::string_view id, glm::vec2 world, float ms, Ease e)
{
	const Id cid = idOf(id);
	CanvasState& c = canvases_[cid];
	c.flyFrom = c.cam.offset;
	c.flyTo = c.cam.centered(world, c.view);
	c.flyEase = e;
	c.flying = true;
	anim.start(flyId(cid), ms);
}

// --- text field ----------------------------------------------------------------

static std::string oneLine(std::string_view s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] == '\r') { if (i + 1 < s.size() && s[i + 1] == '\n') continue; out += ' '; }
		else if (s[i] == '\n') out += ' ';
		else out += s[i];
	}
	return out;
}

// typed text without control characters (keeps '\n' only when allowed)
static std::string cleaned(std::string_view s, bool allowNewline)
{
	std::string out;
	for (char c : s)
	{
		if (c == '\r') continue;
		if (static_cast<unsigned char>(c) < 32 && !(c == '\n' && allowNewline)) continue;
		out += c;
	}
	return out;
}

bool Context::textField(const Rect& r, std::string_view idStr, std::string& value, const TextFieldOptions& opt)
{
	const Id id = idOf(idStr);
	FieldState& st = fields_[id];
	TextEdit& ed = st.edit;
	const MeasureFn mf = measure();
	const unsigned left = 1u << SDL_BUTTON_LEFT;
	const float lh = lineHeight();
	const float asc = font_->ascent(theme.fontScale), desc = font_->descent(theme.fontScale);
	const float pad = theme.padding;
	Rect inner = r.inset(pad);
	if (!opt.multiline) { inner.y = r.y; inner.h = r.h; }
	const bool multi = opt.multiline;
	const bool wrapping = multi && opt.wrap;

	// the app's string is the truth; pick up outside changes
	if (ed.text() != value)
	{
		const size_t was = ed.caret();
		ed.setText(value);
		ed.setCaret(std::min(was, value.size()));
	}

	auto relayout = [&] {
		const float ww = wrapping ? inner.w : 0.0f;
		if (st.wrappedText == ed.text() && st.wrappedWidth == ww && st.wrappedScale == theme.fontScale && st.wrappedFont == font_ && !st.lines.empty()) return;
		if (multi) st.lines = wrap(ed.text(), ww, mf);
		else st.lines = { { 0, ed.text().size() } };
		st.wrappedText = ed.text();
		st.wrappedWidth = ww;
		st.wrappedScale = theme.fontScale;
		st.wrappedFont = font_;
	};
	relayout();

	// offset in the text under a window position
	auto offsetAt = [&](glm::vec2 p) -> size_t {
		size_t li = 0;
		if (multi)
		{
			const float ly = p.y - inner.y + st.scrollY;
			li = static_cast<size_t>(std::clamp(std::floor(ly / lh), 0.0f, static_cast<float>(st.lines.size() - 1)));
		}
		const float x = p.x - inner.x + (multi ? 0.0f : st.scrollX);
		return indexInLines(ed.text(), st.lines, li, x, mf);
	};

	// --- mouse ---
	bool hot = canHover(id, r);
	const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
	const glm::vec2 m = mousePos();
	if (hot && (in_.pressed & left))
	{
		pressClaimed_ = true;
		focus_ = id;
		active_ = id;
		lastInputMs_ = SDL_GetTicks();
		const size_t at = offsetAt(m);
		if (in_.clicks >= 3 && multi)
		{
			const LineRange& ln = st.lines[lineOfOffset(st.lines, at)];
			ed.setCaret(ln.start);
			ed.setCaret(ln.end, true);
		}
		else if (in_.clicks == 2) ed.selectWordAt(at);
		else ed.setCaret(at, shift);
	}
	else if (active_ == id && (down_ & left) && !(in_.pressed & left))
	{
		ed.setCaret(offsetAt(m), true);
		// dragging above / below the box scrolls it
		if (multi)
		{
			if (m.y < inner.y) st.scrollY -= lh * 0.5f;
			else if (m.y > inner.y + inner.h) st.scrollY += lh * 0.5f;
		}
		dirty_ = true;
	}
	if (active_ == id && (in_.released & left)) active_ = 0;

	// --- keyboard ---
	bool focused = focus_ == id && disabled_ == 0;
	bool edited = false;
	if (focused)
	{
		auto clipboardSet = [&](const std::string& s) { if (!s.empty()) SDL_SetClipboardText(s.c_str()); };
		for (const Input::Ev& ev : in_.events)
		{
			if (ev.isText)
			{
				if (!opt.readOnly) ed.insert(cleaned(ev.text, false));
				continue;
			}
			const bool ctrl = (ev.mod & SDL_KMOD_CTRL) != 0;
			const bool sh = (ev.mod & SDL_KMOD_SHIFT) != 0;
			switch (ev.key)
			{
			case SDLK_LEFT: ed.left(sh, ctrl); break;
			case SDLK_RIGHT: ed.right(sh, ctrl); break;
			case SDLK_HOME: if (ctrl) ed.textStart(sh); else ed.home(sh); break;
			case SDLK_END: if (ctrl) ed.textEnd(sh); else ed.end(sh); break;
			case SDLK_UP:
				if (multi) { ed.up(st.lines, mf, sh); } else ed.home(sh);
				break;
			case SDLK_DOWN:
				if (multi) { ed.down(st.lines, mf, sh); } else ed.end(sh);
				break;
			case SDLK_BACKSPACE: if (!opt.readOnly) ed.backspace(ctrl); break;
			case SDLK_DELETE: if (!opt.readOnly) ed.del(ctrl); break;
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				if (multi) { if (!opt.readOnly) ed.insert("\n"); }
				else submittedId_ = id;
				break;
			case SDLK_ESCAPE: focus_ = 0; focused = false; break;
			case SDLK_A: if (ctrl) ed.selectAll(); break;
			case SDLK_C: if (ctrl) clipboardSet(ed.selectedText()); break;
			case SDLK_X:
				if (ctrl) { if (opt.readOnly) clipboardSet(ed.selectedText()); else clipboardSet(ed.cut()); }
				break;
			case SDLK_V:
				if (ctrl && !opt.readOnly)
				{
					if (char* clip = SDL_GetClipboardText())
					{
						ed.paste(multi ? cleaned(clip, true) : oneLine(clip));
						SDL_free(clip);
					}
				}
				break;
			default: break;
			}
			// the layout feeds up/down, so keep it in step with every edit
			relayout();
		}
		if (ed.text() != value)
		{
			value = ed.text();
			edited = true;
		}
	}
	relayout();
	textFocused_ = textFocused_ || (focus_ == id && disabled_ == 0);

	// --- scrolling ---
	const size_t caretLine = lineOfOffset(st.lines, ed.caret());
	const LineRange& cl = st.lines[caretLine];
	const float caretX = textWidth(std::string_view(ed.text()).substr(cl.start, std::min(ed.caret(), cl.end) - cl.start));
	const float contentH = static_cast<float>(st.lines.size()) * lh;
	if (multi && hot && wheel_ != 0.0f)
	{
		st.scrollY -= wheel_ * lh * 3.0f;
		wheel_ = 0.0f;
	}
	if (ed.caret() != st.lastCaret || edited)
	{
		if (multi)
		{
			const float top = static_cast<float>(caretLine) * lh;
			if (top < st.scrollY) st.scrollY = top;
			else if (top + lh > st.scrollY + inner.h) st.scrollY = top + lh - inner.h;
		}
		else
		{
			if (caretX < st.scrollX) st.scrollX = caretX;
			else if (caretX > st.scrollX + inner.w - 2) st.scrollX = caretX - inner.w + 2;
		}
		st.lastCaret = ed.caret();
	}
	st.scrollY = std::clamp(st.scrollY, 0.0f, std::max(0.0f, contentH - inner.h));
	if (!multi)
	{
		const float full = textWidth(ed.text());
		st.scrollX = std::clamp(st.scrollX, 0.0f, std::max(0.0f, full - inner.w + 2));
	}
	else st.scrollX = 0;

	// --- draw ---
	const bool isFocused = focus_ == id;
	painter_->roundedRect(r.x, r.y, r.w, r.h, theme.radius, theme.field);
	painter_->outline(r.x, r.y, r.w, r.h, isFocused ? theme.focusWidth : theme.borderWidth, isFocused ? theme.accent : theme.border, theme.radius);

	pushClip(multi ? inner : Rect{ inner.x, r.y + 1, inner.w, r.h - 2 });
	const auto [sa, sb] = ed.selection();
	const std::string_view full = ed.text();
	const float nlW = textWidth(" ") * 0.6f;
	const int first = multi ? std::max(0, static_cast<int>(std::floor(st.scrollY / lh))) : 0;
	const int last = multi ? std::min(static_cast<int>(st.lines.size()) - 1, static_cast<int>(std::floor((st.scrollY + inner.h) / lh))) : 0;
	for (int i = first; i <= last; i++)
	{
		const LineRange& ln = st.lines[i];
		const float top = multi ? inner.y + i * lh - st.scrollY : r.y + (r.h - lh) * 0.5f;
		const float ox = inner.x - (multi ? 0.0f : st.scrollX);
		const float base = top + (lh - (asc + desc)) * 0.5f + asc;

		if (sa != sb && sa <= ln.end && sb >= ln.start)
		{
			const size_t a = std::clamp(sa, ln.start, ln.end), b = std::clamp(sb, ln.start, ln.end);
			float x0 = textWidth(full.substr(ln.start, a - ln.start));
			float x1 = textWidth(full.substr(ln.start, b - ln.start));
			const bool nlSelected = ln.end < full.size() && full[ln.end] == '\n' && sa <= ln.end && sb > ln.end;
			if (nlSelected) x1 += nlW;
			if (x1 > x0) painter_->rect(ox + x0, top, x1 - x0, lh, theme.selection);
		}
		drawText(full.substr(ln.start, ln.end - ln.start), ox, base, theme.text);
	}
	if (full.empty() && !isFocused && opt.placeholder)
	{
		const float top = multi ? inner.y : r.y + (r.h - lh) * 0.5f;
		drawText(opt.placeholder, inner.x, top + (lh - (asc + desc)) * 0.5f + asc, theme.textDim);
	}
	if (isFocused && blinkPhase() == 0)
	{
		const float top = multi ? inner.y + static_cast<float>(caretLine) * lh - st.scrollY : r.y + (r.h - lh) * 0.5f;
		const float cx = inner.x + caretX - (multi ? 0.0f : st.scrollX);
		painter_->rect(std::round(cx), top + 1, 2, lh - 2, theme.caret);
	}
	popClip();

	if (multi && contentH > inner.h)
	{
		const float maxOff = contentH - inner.h;
		const float thumbH = std::max(20.0f, inner.h * inner.h / contentH);
		const float ty = inner.y + st.scrollY / maxOff * (inner.h - thumbH);
		painter_->roundedRect(r.x + r.w - kScrollbarW - 3, ty, kScrollbarW * 0.6f, thumbH, kScrollbarW * 0.3f, theme.scrollThumb);
	}
	return edited;
}

}
