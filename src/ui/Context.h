#pragma once
// -----------------------------------------------------------------------------
// Context.h -- immediate-mode widgets on top of Painter2D
// -----------------------------------------------------------------------------
// Per iteration of the loop:
//
//   while (SDL_WaitEventTimeout(&e, ui.waitTimeoutMs())) ... ui.handleEvent(e);
//   if (ui.needsRedraw()) {
//       ui.beginFrame(w, h);                 // w, h = drawable size in pixels
//       if (ui.button({10, 10, 120, 32}, "Save")) save();
//       ui.textField({10, 60, 300, 32}, "name", name);
//       ui.endFrame();
//       SDL_GL_SwapWindow(window);
//   }
//
// Widgets take their rect in pixels and are identified by their label (or id
// string); "Label##id" shows "Label" but hashes the whole thing, and pushId() scopes
// ids inside a loop. The mouse helpers (hovered, pressed, ...) are the same ones
// the widgets use, so custom widgets behave alike. Call SDL_StartTextInput(window)
// once, text fields need it to receive typed text.
//
// `anim` (ui/Anim.h) ticks at beginFrame; needsRedraw() / waitTimeoutMs() keep
// frames coming every anim.frameMs while something in it moves, and stop after.

#include "ui/Anim.h"
#include "ui/CanvasView.h"
#include "ui/Geometry.h"
#include "ui/Painter2D.h"
#include "ui/TextEdit.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Text;

namespace ui
{

struct Theme
{
	glm::vec4 background{ 0.11f, 0.12f, 0.14f, 1 };
	glm::vec4 panel{ 0.16f, 0.17f, 0.20f, 1 };
	glm::vec4 field{ 0.09f, 0.10f, 0.12f, 1 };
	glm::vec4 border{ 0.27f, 0.29f, 0.34f, 1 };
	glm::vec4 text{ 0.90f, 0.91f, 0.93f, 1 };
	glm::vec4 textDim{ 0.52f, 0.55f, 0.60f, 1 };
	glm::vec4 accent{ 0.30f, 0.55f, 0.95f, 1 };
	glm::vec4 accentText{ 1, 1, 1, 1 };
	glm::vec4 button{ 0.22f, 0.24f, 0.29f, 1 };
	glm::vec4 buttonHover{ 0.28f, 0.31f, 0.37f, 1 };
	glm::vec4 buttonActive{ 0.20f, 0.42f, 0.78f, 1 };
	glm::vec4 selection{ 0.25f, 0.45f, 0.80f, 0.55f };
	glm::vec4 caret{ 0.95f, 0.95f, 0.95f, 1 };
	glm::vec4 scrollThumb{ 0.45f, 0.48f, 0.55f, 0.8f };
	float padding = 8.0f;
	float radius = 5.0f;
	float borderWidth = 1.0f;
	float focusWidth = 1.5f;   // a focused text field's outline
	float fontScale = 1.0f;
	// accessibility: 1 is full motion, 0 lands every animation at once (handed to anim each frame)
	float motionScale = 1.0f;
	// accessibility: the app should leave out flashes and sudden changes (the module draws none)
	bool reduceFlashing = false;
};

enum class Align { Left, Center, Right };

struct TextFieldOptions
{
	bool multiline = false;
	bool wrap = true; // multiline only
	bool readOnly = false;
	const char* placeholder = nullptr;
};

struct TabResult
{
	int clicked = -1; // tab body clicked
	int closed = -1;  // close button clicked
};

struct CanvasInput
{
	bool clicked = false;    // pressed and released without dragging past the threshold
	glm::vec2 world{ 0, 0 }; // where, in world units
	bool panning = false;    // a drag is moving the view
};

class Context
{
public:
	Theme theme;
	Anim anim;

	// font must stay alive as long as the context; it should have the Latin charset
	// if the UI shows accents.
	void init(Painter2D& painter, Text& font);

	// --- frame -------------------------------------------------------------
	// Feed every SDL event. Returns true for keyboard / text events a focused text
	// field wants, so the app can skip its own key handling for them.
	bool handleEvent(const SDL_Event& e);
	void beginFrame(int width, int height);
	void endFrame();

	// True when something changed since the last frame (input, a window event,
	// requestRedraw), the caret flips, or anim has a frame due. For event-driven loops.
	bool needsRedraw() const;
	// Milliseconds the loop may sleep before the next of those, -1 for forever.
	int waitTimeoutMs() const;
	void requestRedraw() { dirty_ = true; }
	// The shortest time between two frames: input inside it waits for it (0 = none;
	// 33 caps every redraw at 30 fps on a software renderer).
	int minFrameMs = 0;

	// --- mouse helpers (this frame, in the current transform's units) ---------
	glm::vec2 mousePos() const;
	// inside r and inside the current clip (so rows scrolled out of view aren't hot);
	// false for everything inside pushDisabled
	bool hovered(const Rect& r) const;
	bool mouseDown(int button = SDL_BUTTON_LEFT) const { return down_ & (1u << button); }
	// press / release of the button happened since the last frame, over r
	bool pressed(const Rect& r, int button = SDL_BUTTON_LEFT) const;
	bool released(const Rect& r, int button = SDL_BUTTON_LEFT) const;
	bool doubleClicked(const Rect& r) const; // second left press of a double click, over r
	// mouse movement since the last frame, while a button is held
	glm::vec2 dragDelta() const { return dragDelta_ / painter_->transform().scale; }
	// wheel steps since the last frame, positive = away from the user (scroll up)
	float wheel() const { return wheel_; }
	void consumeWheel() { wheel_ = 0; }

	// --- state -------------------------------------------------------------
	void pushId(std::string_view id);
	void popId();
	void setFocus(std::string_view id);
	void blur() { focus_ = 0; dirty_ = true; }
	bool textFocused() const { return textFocused_; }
	bool wantsKeyboard() const { return textFocused_; }
	// Enter was pressed in this single-line field during the last frame
	bool submitted(std::string_view id) const;

	// clip for hit-testing and drawing together
	void pushClip(const Rect& r);
	void popClip();
	// Moves and scales drawing and hit-testing together: in between, rects and the
	// mouse helpers are in the pushed units (widgets in a zoomed view still work).
	void pushTransform(glm::vec2 offset, float scale = 1.0f);
	void popTransform();
	// Widgets in between draw but don't react, to the mouse or to keys (under a modal).
	void pushDisabled();
	void popDisabled();
	bool disabled() const { return disabled_ > 0; }
	// Labels, widgets and the text metrics use this font until popFont.
	void pushFont(Text& font);
	void popFont();

	// --- canvas: a pannable, zoomable view of a world ------------------------------
	// A drag pans it (a press that moves less than its dragThreshold is a click), the
	// wheel zooms at the pointer. Between begin and end, drawing and hit-testing are in
	// world units, clipped to the view. Widgets inside take their presses first.
	CanvasInput beginCanvas(const Rect& view, std::string_view id);
	void endCanvas();
	// its camera: fit it, zoom it from keys, set its limits (kept by id)
	CanvasView& canvas(std::string_view id);
	// glides it so world sits in the middle of its view (cut short by a drag or the wheel)
	void flyCanvas(std::string_view id, glm::vec2 world, float ms, Ease ease = Ease::InOut);

	// --- widgets -----------------------------------------------------------
	void label(float x, float y, std::string_view s, std::optional<glm::vec4> color = {});
	void label(const Rect& r, std::string_view s, Align align = Align::Left, std::optional<glm::vec4> color = {});
	bool button(const Rect& r, std::string_view label);
	bool checkbox(const Rect& r, std::string_view label, bool& value); // true when toggled
	void progressBar(const Rect& r, float fraction, std::string_view label = "");
	// true when the text changed
	bool textField(const Rect& r, std::string_view id, std::string& value, const TextFieldOptions& opt = {});
	TabResult tabBar(const Rect& r, const std::vector<std::string>& labels, int active, bool closable = true);

	// Draw the content at (view.x, view.y - returned offset); it is clipped to view.
	// contentHeight is its full height. Wheel and the scrollbar thumb scroll it.
	float beginScroll(const Rect& view, std::string_view id, float contentHeight);
	void endScroll();

	// --- text metrics at the theme's font scale --------------------------------
	float textWidth(std::string_view s) const;
	float lineHeight() const;
	// Painter and font, for drawing custom things between widgets
	Painter2D& painter() { return *painter_; }
	Text& font() { return *font_; }

private:
	using Id = uint32_t;

	struct Input
	{
		unsigned pressed = 0, released = 0; // bit per SDL button
		int clicks = 0;
		float wheel = 0;
		glm::vec2 drag{ 0, 0 };
		struct Ev { bool isText = false; std::string text; SDL_Keycode key = 0; SDL_Keymod mod = 0; };
		std::vector<Ev> events;
	};

	struct FieldState
	{
		TextEdit edit;
		float scrollX = 0, scrollY = 0;
		std::vector<LineRange> lines;
		std::string wrappedText;
		float wrappedWidth = -1;
		float wrappedScale = -1;
		const Text* wrappedFont = nullptr;
		size_t lastCaret = std::string::npos;
		bool wasEnter = false;
	};

	struct ScrollFrame { Rect view; Id id; float contentH; };

	struct CanvasState
	{
		CanvasView cam;
		Rect view;                  // as of the last beginCanvas
		glm::vec2 pressAt{ 0, 0 };  // where the press that owns it was, outside units
		glm::vec2 offsetAtPress{ 0, 0 };
		bool moved = false;         // that press became a pan
		bool clickNext = false;     // pressed and released in one frame: the next one reports it
		bool flying = false;
		glm::vec2 flyFrom{ 0, 0 }, flyTo{ 0, 0 };
		Ease flyEase = Ease::InOut;
	};

	Id idOf(std::string_view label) const;
	static std::string_view shown(std::string_view label);
	bool canHover(Id id, const Rect& r) const;
	bool clickRegion(const Rect& r, Id id, bool& hot);
	void drawText(std::string_view s, float x, float baseline, const glm::vec4& color);
	void drawTextIn(const Rect& r, std::string_view s, Align align, const glm::vec4& color);
	int blinkPhase() const;
	int capWaitMs() const;
	MeasureFn measure() const;

	Painter2D* painter_ = nullptr;
	Text* font_ = nullptr;
	Text* baseFont_ = nullptr;
	std::vector<Text*> fontStack_;
	int disabled_ = 0;
	int w_ = 0, h_ = 0;

	Input pending_, in_;
	// window pixels; the helpers turn them into the current transform's units
	glm::vec2 mouse_{ -1, -1 };
	glm::vec2 dragDelta_{ 0, 0 };
	float wheel_ = 0;
	unsigned down_ = 0;
	float density_ = 1.0f;
	bool dirty_ = true;
	uint64_t lastInputMs_ = 0;
	uint64_t lastFrameMs_ = 0;
	mutable int drawnPhase_ = -1;

	std::vector<Id> idStack_;
	std::vector<Rect> clips_;  // window pixels
	Id active_ = 0;
	Id focus_ = 0;
	bool textFocused_ = false;
	bool pressClaimed_ = false;
	bool focusLost_ = false;
	Id submittedId_ = 0;

	std::unordered_map<Id, FieldState> fields_;
	std::unordered_map<Id, float> scroll_;
	std::vector<ScrollFrame> scrollStack_;
	std::unordered_map<Id, CanvasState> canvases_;
	std::vector<Id> canvasStack_;
};

}
