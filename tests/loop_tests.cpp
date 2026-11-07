// Tests for ui/Motion (on a hand-driven clock) and ui/Loop (real time, SDL events only,
// no window). Plain asserts, one binary: the loop_tests CMake target.
#include "ui/Loop.h"
#include "ui/Motion.h"

#include <SDL3/SDL.h>
#include <unistd.h>

#include <cmath>
#include <iostream>
#include <thread>

static int gPassed = 0, gFailed = 0;

#define CHECK(...) do { \
	if (__VA_ARGS__) gPassed++; \
	else { gFailed++; std::cout << "FAIL line " << __LINE__ << ": " #__VA_ARGS__ "\n"; } \
} while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }

static double gTime = 0;
static void at(double t) { gTime = t; ui::motion::frame(); }

static void testBezier()
{
	using namespace ui::curve;
	CHECK(linear(0) == 0 && linear(1) == 1);
	CHECK(near(linear(0.3f), 0.3f) && near(linear(0.77f), 0.77f));
	// CSS ease (0.25, 0.1, 0.25, 1) at x = 0.5 is ~0.8024
	CHECK(near(ui::CubicBezier{ 0.25f, 0.1f, 0.25f, 1 }(0.5f), 0.8024f, 2e-3f));
	for (const ui::CubicBezier& c : { enter, fadeIn, leave, standard })
	{
		float prev = 0;
		bool monotonic = true;
		for (int i = 1; i <= 100; i++)
		{
			const float y = c(i / 100.0f);
			monotonic = monotonic && y >= prev - 1e-5f;
			prev = y;
		}
		CHECK(monotonic && near(prev, 1));
	}
	CHECK(enter(0.5f) > 0.8f); // fast out of the gate, like picom's window opens
	CHECK(leave(0.5f) < 0.3f);  // slow start, fast finish
}

static void testTween()
{
	ui::motion::setClock([] { return gTime; });
	at(0);
	ui::Tween<float> t{ 0.0f, 1.0f, ui::curve::linear };
	CHECK(t.value() == 0 && !t.moving() && !ui::motion::wantsFrame());

	gTime = 10; // set() at clock 10 while the last frame was at 0: no jump on the next frame
	t.set(100);
	CHECK(ui::motion::wantsFrame());
	at(10);
	CHECK(near(t.value(), 0) && ui::motion::wantsFrame());
	at(10.25);
	CHECK(near(t.value(), 25));
	at(10.5);
	CHECK(near(t.value(), 50));

	t.set(100); // same target: keeps going
	at(10.75);
	CHECK(near(t.value(), 75));

	t.set(0); // interrupted: from what's on screen (75), full duration back
	at(11.25);
	CHECK(near(t.value(), 37.5f));
	at(11.75);
	CHECK(t.value() == 0 && !t.moving() && !ui::motion::wantsFrame());

	ui::Tween<glm::vec4> c{ glm::vec4(0), 1.0f, ui::curve::linear };
	at(12);
	c.set(glm::vec4(1, 0.5f, 0, 1));
	at(12.5);
	CHECK(near(c.value().y, 0.25f) && near(c.value().w, 0.5f));

	ui::motion::setReduced(true);
	t.set(42);
	CHECK(t.value() == 42 && !t.moving());
	ui::motion::setReduced(false);
}

static void testSpring()
{
	ui::motion::setClock([] { return gTime; });
	at(100);
	ui::Spring<float> s{ 0.0f, 0.35f, 1.0f, 0.001f }; // critically damped
	s.set(1);
	double settledAt = -1;
	float peak = 0;
	for (int i = 1; i <= 300 && settledAt < 0; i++) // 60 fps for up to 5 s
	{
		at(100 + i / 60.0);
		const float v = s.value();
		peak = std::max(peak, v);
		if (!s.moving()) settledAt = i / 60.0;
	}
	CHECK(settledAt > 0.2 && settledAt < 1.5);
	CHECK(peak <= 1.0005f); // no overshoot at damping 1
	CHECK(s.value() == 1 && !ui::motion::wantsFrame());

	ui::Spring<float> b{ 0.0f, 0.35f, 0.5f, 0.001f }; // bouncy
	at(200);
	b.set(1);
	peak = 0;
	for (int i = 1; i <= 120; i++) { at(200 + i / 60.0); peak = std::max(peak, b.value()); }
	CHECK(peak > 1.05f);

	// retargeting keeps velocity: reversing mid-flight still moves forward a bit first
	ui::Spring<float> r{ 0.0f, 0.5f, 1.0f };
	at(300);
	r.set(100);
	at(300.1);
	const float before = r.value();
	CHECK(r.velocity() > 0);
	r.set(0);
	at(300.12);
	CHECK(r.value() > before);

	ui::Spring<glm::vec2> p{ glm::vec2(0), 0.3f, 0.9f, 0.1f };
	at(400);
	p.set({ 300, -40 });
	int steps = 0;
	for (; steps < 300 && p.moving(); steps++) { at(400 + (steps + 1) / 60.0); p.value(); }
	CHECK(!p.moving() && p.value() == glm::vec2(300, -40) && steps > 10);

	// a set() long after the last frame snaps instead of integrating the gap
	ui::Spring<float> late{ 0.0f };
	at(500);
	late.set(1);
	at(510);
	CHECK(late.value() == 1 && !late.moving());
	ui::motion::setClock(nullptr);
}

static void testLoop()
{
	ui::Loop loop;
	int events = 0;
	const auto onEvent = [&](const SDL_Event&) { events++; };

	CHECK(loop.next(onEvent) && loop.frames() == 1); // the first frame is free

	int fired = 0;
	const Uint64 t0 = SDL_GetTicks();
	loop.after(0.05, [&] { fired++; loop.requestFrame(); });
	CHECK(loop.next(onEvent));
	const Uint64 waited = SDL_GetTicks() - t0;
	CHECK(fired == 1 && waited >= 45 && waited < 500);

	int ticks = 0;
	const int every = loop.every(0.02, [&] { if (++ticks == 3) loop.requestFrame(); });
	CHECK(loop.next(onEvent) && ticks == 3);
	loop.cancel(every);
	loop.after(0.08, [&] { loop.requestFrame(); });
	CHECK(loop.next(onEvent) && ticks == 3); // cancelled: no more ticks while waiting

	// a watched pipe: the callback runs on this thread, once per write, and the watch re-arms
	int fds[2];
	CHECK(pipe(fds) == 0);
	const std::thread::id self = std::this_thread::get_id();
	int reads = 0;
	bool sameThread = true;
	const int w = loop.watch(fds[0], [&] {
		char c;
		if (read(fds[0], &c, 1) == 1) reads++;
		sameThread = sameThread && std::this_thread::get_id() == self;
		loop.requestFrame();
	});
	std::thread writer([&] { SDL_Delay(30); (void)!write(fds[1], "a", 1); });
	CHECK(loop.next(onEvent) && reads == 1);
	writer.join();
	(void)!write(fds[1], "b", 1);
	CHECK(loop.next(onEvent) && reads == 2 && sameThread);
	loop.cancel(w);
	(void)!write(fds[1], "c", 1);
	loop.after(0.05, [&] { loop.requestFrame(); });
	CHECK(loop.next(onEvent) && reads == 2);
	close(fds[0]);
	close(fds[1]);

	// an animation keeps frames coming, then the loop goes back to sleep
	ui::Tween<float> t{ 0.0f, 0.1f };
	t.set(1);
	int animFrames = 0;
	while (t.moving() && animFrames < 1000)
	{
		CHECK(loop.next(onEvent));
		t.value();
		animFrames++;
		SDL_Delay(5); // stands in for vsync
	}
	CHECK(!t.moving() && animFrames >= 5);
	const uint64_t frames = loop.frames();
	loop.after(0.06, [&] { loop.quit(); });
	CHECK(!loop.next(onEvent) && loop.frames() == frames); // nothing asked to draw

	// SDL_EVENT_QUIT is passed on, then next() returns false
	ui::Loop q;
	CHECK(q.next(onEvent)); // first frame
	SDL_Event quit{};
	quit.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&quit);
	const int before = events;
	CHECK(!q.next(onEvent) && events == before + 1);
}

int main()
{
	testBezier();
	testTween();
	testSpring();
	if (!SDL_Init(SDL_INIT_EVENTS)) { std::cout << "SDL_Init: " << SDL_GetError() << "\n"; return 1; }
	testLoop();
	SDL_Quit();
	std::cout << gPassed << " passed, " << gFailed << " failed\n";
	return gFailed ? 1 : 0;
}
