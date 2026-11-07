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
// Several windows (one GL context, see createSharedWindow): requestFrame(window) asks
// for a frame of that window only, and after next() returns true, due(window) says
// whether it needs drawing this time. Draw each window between beginWindow(id) and
// endWindow(id): a tween or spring still moving there keeps that window (alone) due
// for the next frame. Expose, show and resize events make their window due. What
// isn't tied to a window (requestFrame(), motion read outside those brackets, an
// attached Context) makes every window due, so a one-window app needs none of this.
//
// Watched fds are polled by one helper thread that wakes the loop with an SDL event; it
// stops polling an fd while its callback is pending, so a callback must read what is
// there (or cancel the watch on EOF), or it is called again at once; likewise a POLLOUT
// watch must write or drop POLLOUT with setEvents.

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
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
	// the same for any poll() events (POLLIN, POLLOUT, ...); fn gets the revents.
	// setEvents changes what an existing watch waits for; 0 pauses it.
	int watch(int fd, short events, std::function<void(short revents)> fn);
	void setEvents(int id, short events);
#endif
	void cancel(int id); // a timer or a watch; unknown ids are ignored

	void requestFrame() { frameRequested_ = true; }
	void requestFrame(SDL_WindowID window) { pending_.insert(window); }
	// after next() returned true: whether this frame draws that window
	bool due(SDL_WindowID window) const { return allDue_ || due_.count(window) != 0; }
	void beginWindow(SDL_WindowID window);
	void endWindow(SDL_WindowID window);
	// Frames at most this often (0 = no cap, the default: vsync paces them). For
	// windows swapped without vsync, e.g. several sharing a context, where waiting a
	// vblank per window would divide the frame rate by their number.
	void setMaxFps(double fps) { minInterval_ = fps > 0 ? 1.0 / fps : 0.0; }
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
	std::set<SDL_WindowID> pending_, animating_; // for the next frame
	std::set<SDL_WindowID> due_;                 // this frame
	bool allDue_ = true;
	bool wantOutside_ = false;                   // motion's flag from outside the brackets
	double minInterval_ = 0, lastFrame_ = -1;
	bool quit_ = false;
	uint64_t frames_ = 0;
};

}
