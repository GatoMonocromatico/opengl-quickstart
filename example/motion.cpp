// -----------------------------------------------------------------------------
// motion.cpp -- demo of what moves in src/ui: ui::Anim (timers, tweens, springs,
// rolling counters), a pannable zoomable canvas, transforms, alpha, dithering,
// an app's own fragment shader (example/shaders/sheen.frag) and a band blit of
// the frame
// -----------------------------------------------------------------------------
// Event-driven like widgets.cpp: it sleeps until input and draws at 30 fps only
// while something moves ("frames drawn" stops counting when nothing does).
// QS_SHOT=<png> saves a frame QS_SHOT_MS (default 300) ms in, with a card picked
// and the counters rolling; QS_DIM=1 puts the board behind the modal, QS_SLIP=1
// catches a band blit.
#define GLM_ENABLE_EXPERIMENTAL

#include <glad/glad.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "core/Window.h"
#include "render/Framebuffer.h"
#include "render/Text.h"
#include "ui/Color.h"
#include "ui/Context.h"
#include "ui/Fonts.h"
#include "ui/Painter2D.h"
#include "util/DebugHooks.h"

namespace
{

constexpr int kCards = 12;
constexpr float kCardW = 150, kCardH = 84, kStepX = 180, kStepY = 116;
const ui::Rect kWorld{ 0, 0, 3 * kStepX + kCardW, 2 * kStepY + kCardH };

ui::Rect cardRect(int i)
{
	return { static_cast<float>(i % 4) * kStepX, static_cast<float>(i / 4) * kStepY, kCardW, kCardH };
}

// A counter in digit slots, from a rolling value: each wheel turns only while the
// one to its right goes from 9 to 0, like an odometer.
void digits(ui::Painter2D& p, Text& font, float value, float x, float y, int n, const glm::vec4& ink, const glm::vec4& slot)
{
	const float w = 22, h = 30;
	float v = std::max(0.0f, value);
	for (int i = n - 1; i >= 0; i--)
	{
		const float pos = std::fmod(v, 10.0f);
		const float sx = x + static_cast<float>(i) * (w + 3);
		p.rect(sx, y, w, h, slot);
		p.pushClip(sx, y, w, h);
		const int d = static_cast<int>(std::floor(pos));
		const float f = pos - static_cast<float>(d);
		for (int k = 0; k < 2; k++)
		{
			const std::string s = std::to_string((d + k) % 10);
			p.text(font, s, sx + (w - font.width(s)) * 0.5f, y + (h + font.ascent()) * 0.5f + (static_cast<float>(k) - f) * h, ink);
		}
		p.popClip();
		v = std::floor(v / 10.0f) + (pos > 9.0f ? pos - 9.0f : 0.0f);
	}
}

}

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	SDLState state = initialize(900, 600, { .title = "UI motion", .appId = "opengl-quickstart-motion", .preferDedicatedGpu = false });
	if (!state.successfullyInitialized) return 1;

	ui::Painter2D painter;
	ui::Fonts fonts;
	ui::Context ui;
	int sheen = 0;
	try
	{
		painter.create();
		sheen = painter.addProgram(MOTION_SHADER_DIR "sheen.frag");
		ui.init(painter, fonts.get(QS_FONT_DIR "MesloLGSNerdFontMono-Regular.ttf", 15.0f));
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		cleanup(state);
		return 1;
	}

	// the sheen's grain: a small noise tile for one of its samplers
	std::vector<unsigned char> noise(64 * 64);
	std::mt19937 rng(7);
	for (unsigned char& c : noise) c = static_cast<unsigned char>(rng() & 255u);
	unsigned grain = 0;
	glGenTextures(1, &grain);
	glBindTexture(GL_TEXTURE_2D, grain);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 64, 64, 0, GL_RED, GL_UNSIGNED_BYTE, noise.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBindTexture(GL_TEXTURE_2D, 0);
	painter.bindTexture(sheen, "uGrain", grain);

	const char* shotPath = std::getenv("QS_SHOT");
	const uint64_t shotMs = std::getenv("QS_SHOT_MS") ? std::strtoull(std::getenv("QS_SHOT_MS"), nullptr, 10) : 300;
	const bool shotSlip = std::getenv("QS_SLIP") != nullptr;
	const uint64_t started = SDL_GetTicks();

	int picked = -1, value = 7, frames = 0, ticks = 0;
	bool dim = std::getenv("QS_DIM") != nullptr, reduced = false, fitted = false, slipped = false;
	std::string status;
	bool statusOn = false;
	int said = 0;
	// a line that shows for a while, then fades
	auto say = [&](std::string s) {
		status = std::move(s);
		statusOn = true;
		const int mine = ++said;
		ui.anim.after(1800, [&, mine] { if (mine == said) statusOn = false; });
	};

	// the band blit: a band of the finished frame drawn back shifted, for 120 ms
	ScreenCapture capture;
	std::mt19937 dice(11);
	float slipY = 0, slipH = 0, slipDx = 0;
	auto slip = [&] {
		slipY = 70.0f + static_cast<float>(dice() % 420);
		slipH = 14.0f + static_cast<float>(dice() % 30);
		slipDx = 4.0f + static_cast<float>(dice() % 9);
		ui.anim.start("slip", 120);
	};

	bool running = true;
	while (running)
	{
		SDL_Event e;
		bool got = shotPath ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, ui.waitTimeoutMs());
		while (got)
		{
			if (e.type == SDL_EVENT_QUIT) running = false;
			else if (!ui.handleEvent(e) && e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
			got = SDL_PollEvent(&e);
		}
		if (shotPath)
		{
			// the scripted moment: a pick and a count after the first frame, a slip just before the shot
			if (frames == 1 && picked < 0) { picked = 5; value = 19; say("Picked card 6."); }
			if (shotSlip && !slipped && SDL_GetTicks() - started + 60 >= shotMs) { slip(); slipped = true; }
			ui.requestRedraw();
			SDL_Delay(4);
		}
		if (!ui.needsRedraw()) continue;

		SDL_GetWindowSizeInPixels(state.window, &state.width, &state.height);
		glViewport(0, 0, state.width, state.height);
		const ui::Theme& th = ui.theme;
		glClearColor(th.background.r, th.background.g, th.background.b, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		ui.theme.motionScale = reduced ? 0.0f : 1.0f;
		ui.beginFrame(state.width, state.height);
		ui::Painter2D& p = ui.painter();
		const float W = static_cast<float>(state.width), H = static_cast<float>(state.height);
		frames++;

		// the pointer turns the sheen; set before its first quad of the frame
		glUseProgram(p.glProgram(sheen));
		glUniform2f(glGetUniformLocation(p.glProgram(sheen), "uPointer"), ui.mousePos().x, ui.mousePos().y);

		ui.label(16, 14, "Drag the board to pan, the wheel zooms at the pointer, a click picks a card.");

		// --- the board: a canvas, drawn and hit-tested in world units ---
		const ui::Rect view{ 16, 48, 560, H - 64 };
		if (!fitted)
		{
			ui.canvas("board").fit(kWorld, view, 16);
			fitted = true;
		}
		p.rect(view.x, view.y, view.w, view.h, th.field);
		if (dim) ui.pushDisabled();
		const ui::CanvasInput in = ui.beginCanvas(view, "board");
		const float z = ui.canvas("board").zoom;
		// made at the size it's shown at and drawn at 1 / zoom, so it's sharp at any zoom
		Text& title = fonts.get(QS_FONT_DIR "MesloLGSNerdFontMono-Regular.ttf", std::round(15.0f * z));
		for (int i = 0; i < kCards; i++)
		{
			const ui::Rect r = cardRect(i);
			const bool hot = ui.hovered(r) && !in.panning;
			p.roundedRect(r.x, r.y, r.w, r.h, 6, hot ? th.buttonHover : th.panel);
			// the reveal: the app's shader, grown from the middle by a tween
			const float grow = ui.anim.tween({ "grow", i }, picked == i ? 1.25f : 0.0f, picked == i ? 650.0f : 300.0f);
			if (grow > 0.001f)
			{
				p.pushProgram(sheen);
				p.params({ grow, 0, 0, 0 });
				p.roundedRect(r.x, r.y, r.w, r.h, 6, glm::vec4(1.0f));
				p.popProgram();
			}
			p.circle(r.x + r.w - 18, r.y + 18, 7, glm::vec4(ui::oklch(0.74f, 0.13f, 30.0f * static_cast<float>(i)), 1.0f));
			p.text(title, "Card " + std::to_string(i + 1), r.x + 12, r.y + 26, picked == i ? ui::rgb(0x10131a) : th.text, 1.0f / z);
		}
		// the marker springs after the picked card, overshooting a little
		if (picked >= 0)
		{
			const ui::Rect r = cardRect(picked);
			const float mx = ui.anim.spring("markX", r.x, 220, 16), my = ui.anim.spring("markY", r.y, 220, 16);
			p.outline(mx - 5, my - 5, r.w + 10, r.h + 10, 2, th.accent, 9);
		}
		ui.endCanvas();
		if (dim) ui.popDisabled();
		if (in.clicked)
		{
			picked = -1;
			for (int i = 0; i < kCards; i++)
				if (cardRect(i).contains(in.world)) picked = i;
			say(picked < 0 ? "Nothing there." : "Picked card " + std::to_string(picked + 1) + ".");
		}

		if (dim)
		{
			// a modal's checker: every other pixel, from the 4x4 ordered pattern
			p.pushDither(0.5f);
			p.rect(view.x, view.y, view.w, view.h, ui::rgb(0x05070a));
			p.popDither();
			const ui::Rect box{ view.x + view.w * 0.5f - 160, view.y + view.h * 0.5f - 50, 320, 100 };
			p.rect(box.x, box.y, box.w, box.h, th.panel);
			p.outline(box.x, box.y, box.w, box.h, 1, th.border);
			ui.label(box.x + 16, box.y + 14, "The board is behind a modal.");
			if (ui.button({ box.x + 16, box.y + 54, 120, 32 }, "Close")) dim = false;
		}

		// --- the side: counters and keys ---
		const float x0 = 600;
		float y = 48;
		ui.label(x0, y, "Rolling to " + std::to_string(value));
		y += 28;
		int t = 0;
		const float stepped = ui.anim.roll("stepped", static_cast<float>(value), {}, &t);
		ticks += t;
		const float eased = ui.anim.roll("eased", static_cast<float>(value),
			{ .stepMs = 70, .baseMs = 140, .maxMs = 900, .ease = ui::Ease::Back, .stepped = false });
		ui.pushFont(fonts.get(QS_FONT_DIR "SpaceMono-Bold.ttf", 22.0f));
		digits(p, ui.font(), stepped, x0, y, 3, th.text, th.field);
		digits(p, ui.font(), eased, x0 + 130, y, 3, th.text, th.field);
		ui.popFont();
		y += 36;
		ui.label(x0, y, "stepped", th.textDim);
		ui.label(x0 + 130, y, "eased", th.textDim);
		y += 28;
		if (ui.button({ x0, y, 80, 30 }, "+1")) value += 1;
		if (ui.button({ x0 + 90, y, 80, 30 }, "+12")) value += 12;
		if (ui.button({ x0 + 180, y, 80, 30 }, "-5")) value = std::max(0, value - 5);
		y += 38;
		ui.label(x0, y, "ticks: " + std::to_string(ticks), th.textDim);
		y += 40;

		ui.label(x0, y, "The board, " + std::to_string(static_cast<int>(std::round(z * 100))) + "%");
		y += 28;
		if (ui.button({ x0, y, 125, 30 }, "Fit")) ui.canvas("board").fit(kWorld, view, 16);
		if (ui.button({ x0 + 135, y, 125, 30 }, "Fly to 12"))
		{
			const ui::Rect r = cardRect(kCards - 1);
			ui.flyCanvas("board", { r.x + r.w * 0.5f, r.y + r.h * 0.5f }, 600);
		}
		y += 40;
		ui.checkbox({ x0, y, 270, 30 }, "Behind a modal", dim);
		y += 36;
		ui.checkbox({ x0, y, 270, 30 }, "Reduced motion", reduced);
		y += 36;
		if (ui.button({ x0, y, 125, 30 }, "Slip")) slip();
		y += 48;
		ui.label(x0, y, "frames drawn: " + std::to_string(frames), th.textDim);

		// the last thing said fades out
		const float a = ui.anim.tween("status", statusOn ? 1.0f : 0.0f, statusOn ? 120.0f : 600.0f);
		if (a > 0.0f)
		{
			p.pushAlpha(a);
			ui.label(x0, H - 40, status);
			p.popAlpha();
		}

		// the band blit: the frame so far, captured, a band of it drawn back shifted, the
		// sheen in the gap it leaves
		if (ui.anim.running("slip"))
		{
			p.flush();
			capture.capture(state.width, state.height);
			p.pushProgram(sheen);
			p.params({ 2.0f, 0, 0, 0 });
			p.rect(0, slipY, slipDx, slipH, glm::vec4(1.0f));
			p.popProgram();
			// a capture holds its bottom row first: v runs from 1 down
			p.image(capture.texture, slipDx, slipY, W, slipH, glm::vec4(1.0f), { 0.0f, 1.0f - slipY / H, 1.0f, 1.0f - (slipY + slipH) / H });
		}

		ui.endFrame();
		if (shotPath && SDL_GetTicks() - started >= shotMs)
		{
			saveScreenshot(state, shotPath);
			running = false;
		}
		SDL_GL_SwapWindow(state.window);
	}

	capture.Delete();
	glDeleteTextures(1, &grain);
	fonts.Delete();
	painter.Delete();
	cleanup(state);
	return 0;
}
