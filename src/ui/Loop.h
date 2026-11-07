#pragma once
// -----------------------------------------------------------------------------
// Loop.h -- an event loop that sleeps until something needs a frame
// -----------------------------------------------------------------------------
//   ui::Loop loop;
//   loop.attach(ui);                                  // caret blink, ui.requestRedraw()
//   loop.every(1.0, [&] { loop.requestFrame(); });    // a clock
//   loop.watch(fd, [&] { readIt(fd); loop.requestFrame(); });  // a socket (POSIX)
//   while (loop.next([&](const SDL_Event& e) { ui.handleEvent(e); ... }))
//   {
//       draw();                // tweens and springs read here ask for the next frame
//       SDL_GL_SwapWindow(window);
//   }
//
// next() returns true when a frame is due: requestFrame() was called, an animation is
// still moving (motion::wantsFrame()), an attached Context needs a redraw, or a window
// was exposed or resized. Until then it sleeps in SDL_WaitEventTimeout, waking for SDL
// events, the nearest timer, and watched file descriptors, and runs their callbacks on
// the calling thread. It returns false after quit() or SDL_EVENT_QUIT. While animating,
// frames come as fast as SDL_GL_SwapWindow lets them (vsync); idle, it uses no CPU.
//
// Watched fds are polled by one helper thread that wakes the loop with an SDL event; it
// stops polling an fd while its callback is pending, so a callback must read what is
// there (or cancel the watch on EOF), or it is called again at once.

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ui
{

class Context;

class Loop
{
public:
	Loop();
	~Loop();
	Loop(const Loop&) = delete;
	Loop& operator=(const Loop&) = delete;

	// its needsRedraw() and caret timing drive frames; feed it events yourself in onEvent
	void attach(Context& ui);

	// fn runs on the loop's thread once after `seconds`, or every `seconds`.
	// The id cancels it, as it does watches.
	int after(double seconds, std::function<void()> fn);
	int every(double seconds, std::function<void()> fn);
#ifndef _WIN32
	// fn runs on the loop's thread when fd is readable, hung up or in error
	int watch(int fd, std::function<void()> fn);
#endif
	void cancel(int id); // a timer or a watch; unknown ids are ignored

	void requestFrame() { frameRequested_ = true; }
	void quit() { quit_ = true; }

	// sleeps until the next frame is due; see the top of the file
	bool next(const std::function<void(const SDL_Event&)>& onEvent);
	uint64_t frames() const { return frames_; } // handed out by next() so far

private:
	struct Timer
	{
		int id;
		double due, interval; // interval 0 = once
		std::function<void()> fn;
	};
	struct Watcher;

	void dispatch(const SDL_Event& e, const std::function<void(const SDL_Event&)>& onEvent);
	void runTimers();
	bool frameDue() const;
	int waitMs() const;

	std::vector<Context*> contexts_;
	std::vector<Timer> timers_;
	std::unique_ptr<Watcher> watcher_; // started by the first watch()
	int nextId_ = 1;
	bool frameRequested_ = true; // the first frame
	bool quit_ = false;
	uint64_t frames_ = 0;
};

}
