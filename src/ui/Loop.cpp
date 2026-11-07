#include "ui/Loop.h"

#include "ui/Context.h"
#include "ui/Motion.h"

#include <algorithm>
#include <cmath>
#include <utility>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <thread>
#include <unistd.h>
#endif

namespace ui
{

#ifndef _WIN32
// One thread polls every armed fd plus a control pipe. A ready fd is disarmed and an
// SDL event carrying its id wakes the loop; the loop runs the callback and re-arms it.
struct Loop::Watcher
{
	struct Watch
	{
		int id, fd;
		short events;
		std::function<void(short)> fn;
		bool armed = true;
		short revents = 0; // what poll() saw, handed to fn
	};

	std::mutex m;
	std::vector<Watch> list;
	int ctl[2] = { -1, -1 };
	bool stop = false;
	Uint32 eventType = 0;
	std::thread thread;

	Watcher()
	{
		eventType = SDL_RegisterEvents(1);
		if (pipe2(ctl, O_NONBLOCK | O_CLOEXEC) != 0) ctl[0] = ctl[1] = -1;
		// signals (SIGTERM, SIGINT -> SDL_EVENT_QUIT) must reach the loop's thread, not this one
		sigset_t all, old;
		sigfillset(&all);
		pthread_sigmask(SIG_BLOCK, &all, &old);
		thread = std::thread([this] { run(); });
		pthread_sigmask(SIG_SETMASK, &old, nullptr);
	}
	~Watcher()
	{
		{
			std::lock_guard lock(m);
			stop = true;
		}
		wake();
		thread.join();
		for (int fd : ctl)
			if (fd >= 0) close(fd);
	}
	void wake() const
	{
		const char c = 1;
		if (ctl[1] >= 0) (void)!write(ctl[1], &c, 1);
	}
	void run()
	{
		std::vector<pollfd> fds;
		std::vector<int> ids;
		for (;;)
		{
			fds.assign(1, { ctl[0], POLLIN, 0 });
			ids.assign(1, 0);
			{
				std::lock_guard lock(m);
				if (stop) return;
				for (const Watch& w : list)
					if (w.armed && w.events)
					{
						fds.push_back({ w.fd, w.events, 0 });
						ids.push_back(w.id);
					}
			}
			if (poll(fds.data(), fds.size(), -1) < 0)
			{
				if (errno == EINTR) continue;
				return;
			}
			if (fds[0].revents)
			{
				char buf[64];
				while (read(ctl[0], buf, sizeof buf) > 0) {}
			}
			for (size_t i = 1; i < fds.size(); i++)
			{
				if (!fds[i].revents) continue;
				{
					std::lock_guard lock(m);
					for (Watch& w : list)
						if (w.id == ids[i])
						{
							w.armed = false;
							w.revents = fds[i].revents;
						}
				}
				SDL_Event e{};
				e.type = eventType;
				e.user.code = ids[i];
				SDL_PushEvent(&e);
			}
		}
	}
	// on the loop's thread, for an event this watcher pushed
	void fire(int id)
	{
		std::function<void(short)> fn;
		short revents = 0;
		{
			std::lock_guard lock(m);
			for (const Watch& w : list)
				if (w.id == id)
				{
					fn = w.fn;
					revents = w.revents;
				}
		}
		if (!fn) return; // cancelled while the event was queued
		fn(revents);
		{
			std::lock_guard lock(m);
			for (Watch& w : list)
				if (w.id == id) w.armed = true;
		}
		wake();
	}
	bool remove(int id)
	{
		bool found = false;
		{
			std::lock_guard lock(m);
			const auto it = std::find_if(list.begin(), list.end(), [&](const Watch& w) { return w.id == id; });
			if (it != list.end()) { list.erase(it); found = true; }
		}
		if (found) wake();
		return found;
	}
	void setEvents(int id, short events)
	{
		{
			std::lock_guard lock(m);
			for (Watch& w : list)
				if (w.id == id) w.events = events;
		}
		wake();
	}
};
#else
struct Loop::Watcher {};
#endif

Loop::Loop() = default;
Loop::~Loop() = default;

void Loop::attach(Context& ui) { contexts_.push_back(&ui); }

int Loop::after(double seconds, std::function<void()> fn)
{
	timers_.push_back({ nextId_, motion::clock() + seconds, 0, std::move(fn) });
	return nextId_++;
}

int Loop::every(double seconds, std::function<void()> fn)
{
	timers_.push_back({ nextId_, motion::clock() + seconds, seconds > 0 ? seconds : 1e-3, std::move(fn) });
	return nextId_++;
}

#ifndef _WIN32
int Loop::watch(int fd, std::function<void()> fn)
{
	return watch(fd, POLLIN, [fn = std::move(fn)](short) { fn(); });
}

int Loop::watch(int fd, short events, std::function<void(short revents)> fn)
{
	if (!watcher_) watcher_ = std::make_unique<Watcher>();
	const int id = nextId_++;
	{
		std::lock_guard lock(watcher_->m);
		watcher_->list.push_back({ id, fd, events, std::move(fn) });
	}
	watcher_->wake();
	return id;
}

void Loop::setEvents(int id, short events)
{
	if (watcher_) watcher_->setEvents(id, events);
}
#endif

void Loop::cancel(int id)
{
	const auto it = std::find_if(timers_.begin(), timers_.end(), [&](const Timer& t) { return t.id == id; });
	if (it != timers_.end()) { timers_.erase(it); return; }
#ifndef _WIN32
	if (watcher_) watcher_->remove(id);
#endif
}

void Loop::runTimers()
{
	const double now = motion::clock();
	// collect first: a callback may add or cancel timers
	std::vector<int> due;
	for (const Timer& t : timers_)
		if (t.due <= now) due.push_back(t.id);
	for (int id : due)
	{
		const auto it = std::find_if(timers_.begin(), timers_.end(), [&](const Timer& t) { return t.id == id; });
		if (it == timers_.end()) continue; // cancelled by an earlier callback
		std::function<void()> fn = it->fn;
		if (it->interval > 0)
		{
			it->due += it->interval;
			if (it->due <= now) it->due = now + it->interval; // fell behind (suspend): don't burst
		}
		else timers_.erase(it);
		fn();
	}
}

void Loop::beginWindow(SDL_WindowID window)
{
	(void)window;
	wantOutside_ = motion::wantsFrame();
	motion::setWantsFrame(false);
}

void Loop::endWindow(SDL_WindowID window)
{
	if (motion::wantsFrame()) animating_.insert(window); // still moving there: that window again
	motion::setWantsFrame(wantOutside_);
}

bool Loop::frameDue() const
{
	if (frameRequested_ || motion::wantsFrame() || !pending_.empty() || !animating_.empty()) return true;
	for (const Context* c : contexts_)
		if (c->needsRedraw()) return true;
	return false;
}

int Loop::waitMs() const
{
	const double now = motion::clock();
	if (frameDue())
	{
		// capped: what's left of the interval, a frame once under half a millisecond
		const double left = lastFrame_ + minInterval_ - now;
		return minInterval_ > 0 && left >= 0.0005 ? std::max(1, static_cast<int>(left * 1000.0)) : 0;
	}
	double ms = -1;
	for (const Timer& t : timers_)
	{
		const double d = std::max(0.0, (t.due - now) * 1000.0);
		if (ms < 0 || d < ms) ms = d;
	}
	for (const Context* c : contexts_)
	{
		const int d = c->waitTimeoutMs();
		if (d >= 0 && (ms < 0 || d < ms)) ms = d;
	}
	return ms < 0 ? -1 : static_cast<int>(std::ceil(ms));
}

void Loop::dispatch(const SDL_Event& e, const std::function<void(const SDL_Event&)>& onEvent)
{
#ifndef _WIN32
	if (watcher_ && e.type == watcher_->eventType)
	{
		watcher_->fire(e.user.code);
		return;
	}
#endif
	switch (e.type)
	{
	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_SHOWN:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		if (e.window.windowID) pending_.insert(e.window.windowID);
		else frameRequested_ = true;
		break;
	default: break;
	}
	if (onEvent) onEvent(e);
	if (e.type == SDL_EVENT_QUIT) quit_ = true;
}

bool Loop::next(const std::function<void(const SDL_Event&)>& onEvent)
{
	SDL_Event e;
	for (;;)
	{
		runTimers();
		if (quit_) return false; // also when a timer just asked for it
		const int wait = waitMs();
		if (wait == 0)
		{
			// a frame is due: take what's queued without blocking, then draw
			while (!quit_ && SDL_PollEvent(&e)) dispatch(e, onEvent);
			if (quit_) return false;
			allDue_ = frameRequested_ || motion::wantsFrame();
			for (const Context* c : contexts_) allDue_ = allDue_ || c->needsRedraw();
			due_ = std::move(pending_);
			due_.insert(animating_.begin(), animating_.end());
			pending_.clear();
			animating_.clear();
			frameRequested_ = false;
			motion::frame();
			lastFrame_ = motion::now();
			frames_++;
			return true;
		}
		if (SDL_WaitEventTimeout(&e, wait))
		{
			dispatch(e, onEvent);
			while (!quit_ && SDL_PollEvent(&e)) dispatch(e, onEvent);
		}
	}
}

}
