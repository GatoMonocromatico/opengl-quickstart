// -----------------------------------------------------------------------------
// widgets.cpp -- demo of src/ui: Painter2D, Text (Latin charset) and ui::Context
// -----------------------------------------------------------------------------
// Event-driven: the loop sleeps in SDL_WaitEventTimeout and only draws when
// ui.needsRedraw() says something changed (input, caret blink, "Animate").
// QS_SHOT=<png> draws a few frames, saves one and exits, for scripted screenshots.
#define GLM_ENABLE_EXPERIMENTAL

#include <glad/glad.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "core/Window.h"
#include "render/Text.h"
#include "ui/Context.h"
#include "ui/Painter2D.h"
#include "util/DebugHooks.h"

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;
	SDLState state = initialize(920, 660, { .title = "UI widgets", .appId = "opengl-quickstart-widgets", .preferDedicatedGpu = false });
	if (!state.successfullyInitialized) return 1;

	ui::Painter2D painter;
	Text font;
	ui::Context ui;
	try
	{
		painter.create();
		font.create(QS_FONT_DIR "MesloLGSNerdFontMono-Regular.ttf", 18.0f, Text::Charset::Latin);
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		cleanup(state);
		return 1;
	}
	ui.init(painter, font);
	SDL_StartTextInput(state.window);

	const char* shotPath = std::getenv("QS_SHOT");
	int frames = 0;

	std::vector<std::string> tabs = { "Widgets", "Notes", "Scroll" };
	int activeTab = std::getenv("QS_TAB") ? std::atoi(std::getenv("QS_TAB")) : 0;
	int clicks = 0;
	bool animate = false, dark = true, wrapNotes = true;
	float progress = 0.35f;
	std::string name = "Maria João";
	std::string notes = "Notas de hoje: ação, você, órgão ✓ → •\n\nUm parágrafo mais comprido para mostrar a quebra de linha automática dentro do campo de texto, com seleção, cursor e rolagem até o cursor.\n- primeiro item\n- segundo item";
	std::string status;

	if (shotPath) ui.setFocus("notes");

	bool running = true;
	while (running)
	{
		SDL_Event e;
		// sleep until input, or until the caret has to flip; Animate keeps frames coming
		const int timeout = animate ? 16 : ui.waitTimeoutMs();
		bool got = shotPath ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, timeout);
		while (got)
		{
			if (e.type == SDL_EVENT_QUIT) running = false;
			// the app's own shortcuts only when no text field wants the key
			else if (!ui.handleEvent(e) && e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
			got = SDL_PollEvent(&e);
		}
		if (animate) { progress += 0.004f; if (progress > 1.0f) progress = 0.0f; ui.requestRedraw(); }
		if (shotPath) ui.requestRedraw();
		if (!ui.needsRedraw()) continue;

		SDL_GetWindowSizeInPixels(state.window, &state.width, &state.height);
		glViewport(0, 0, state.width, state.height);
		const ui::Theme& th = ui.theme;
		glClearColor(th.background.r, th.background.g, th.background.b, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		const float W = static_cast<float>(state.width), H = static_cast<float>(state.height);
		const float m = 16.0f, rowH = 34.0f;
		ui.beginFrame(state.width, state.height);

		ui::TabResult t = ui.tabBar({ 0, 0, W, 38 }, tabs, activeTab);
		if (t.clicked >= 0) activeTab = t.clicked;
		if (t.closed >= 0)
		{
			tabs.erase(tabs.begin() + t.closed);
			if (tabs.empty()) tabs.push_back("Widgets");
			activeTab = std::min(activeTab, static_cast<int>(tabs.size()) - 1);
		}
		const std::string& tab = tabs[activeTab];

		float y = 38 + m;
		if (tab == "Widgets")
		{
			ui.label(m, y, "Buttons, checkboxes, progress, one-line field");
			y += ui.lineHeight() + 8;
			if (ui.button({ m, y, 150, rowH }, "Click me")) { clicks++; progress = std::min(1.0f, progress + 0.1f); }
			ui.label({ m + 164, y, 300, rowH }, "clicks: " + std::to_string(clicks), ui::Align::Left, th.textDim);
			y += rowH + 10;
			ui.checkbox({ m, y, 200, rowH }, "Animate", animate);
			ui.checkbox({ m + 210, y, 260, rowH }, "Wrap notes", wrapNotes);
			y += rowH + 10;
			ui.progressBar({ m, y, W - 2 * m, 26 }, progress, std::to_string(int(progress * 100)) + "%");
			y += 26 + 16;
			ui.label(m, y, "Nome");
			y += ui.lineHeight() + 4;
			ui.textField({ m, y, 420, rowH }, "name", name, { .placeholder = "escreva o nome" });
			if (ui.submitted("name")) status = "Enter: " + name;
			y += rowH + 16;
			ui.label(m, y, "ação, você, órgão ✓ → • … “aspas” — traço");
			y += ui.lineHeight() + 8;
			ui.label(m, y, status.empty() ? "(Enter in the field shows up here)" : status, th.textDim);
		}
		else if (tab == "Notes")
		{
			ui.label(m, y, "Multi-line field: wrap, selection, mouse, clipboard");
			y += ui.lineHeight() + 8;
			ui.textField({ m, y, W - 2 * m, H - y - m - 40 }, "notes", notes, { .multiline = true, .wrap = wrapNotes });
			ui.label(m, H - m - 28, std::to_string(notes.size()) + " bytes", th.textDim);
		}
		else
		{
			ui.label(m, y, "Scroll area (wheel or drag the thumb)");
			y += ui.lineHeight() + 8;
			const ui::Rect view{ m, y, W - 2 * m, H - y - m };
			ui.painter().roundedRect(view.x, view.y, view.w, view.h, th.radius, th.panel);
			const float itemH = 30;
			const int items = 60;
			const float off = ui.beginScroll(view, "list", items * itemH);
			for (int i = 0; i < items; i++)
			{
				const ui::Rect row{ view.x + 6, view.y + i * itemH - off, view.w - 20, itemH - 4 };
				if (row.y + row.h < view.y || row.y > view.y + view.h) continue;
				ui.pushId(std::to_string(i));
				if (ui.button(row, "Item " + std::to_string(i + 1) + " - ação ✓##row")) status = "picked " + std::to_string(i + 1);
				ui.popId();
			}
			ui.endScroll();
		}
		ui.label({ W - 360 - m, H - 30, 360, 26 }, status, ui::Align::Right, th.textDim);

		ui.endFrame();
		frames++;
		if (shotPath && frames >= 4)
		{
			saveScreenshot(state, shotPath);
			running = false;
		}
		SDL_GL_SwapWindow(state.window);
	}

	font.Delete();
	painter.Delete();
	cleanup(state);
	return 0;
}
