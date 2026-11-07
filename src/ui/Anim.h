#pragma once
// -----------------------------------------------------------------------------
// Anim.h -- timers, tweens, springs and rolling numbers for an event-driven UI
// -----------------------------------------------------------------------------
// The clock is whatever update() is given (ui::Context passes SDL_GetTicks() at
// each beginFrame), so a frame reads one time and tests can step it by hand.
// Nothing runs between frames. moving() says something is in motion and
// waitTimeoutMs() how long the loop may sleep: frameMs while something moves,
// until the next after() when only those wait, -1 when nothing does. Context
// folds both into its needsRedraw() / waitTimeoutMs().
//
//   anim.start("open", 400, [&] { opened = true; });       // a timer, 0 to 1
//   float k = ui::ease(ui::Ease::Out, anim.progress("open"));
//   float x = anim.tween({ "card", i }, selected ? 20.0f : 0.0f, 200);
//   float n = anim.roll("score", score);                    // a counter running to it
//
// start() is for things that move (frames come while it runs); after() is for
// waiting (no frames until it's due).

#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ui
{

// Linear; In, Out and InOut are cubic; Back is Out with a small overshoot that settles
enum class Ease { Linear, In, Out, InOut, Back };
// t in [0, 1], clamped; 0 and 1 map to 0 and 1
float ease(Ease e, float t);

// What an animation is called: a string, or a string and an index for one per item
// in a loop ({ "card", i }).
struct AnimId
{
	uint32_t hash;
	AnimId(const char* s);
	AnimId(std::string_view s);
	AnimId(std::string_view s, int index);
	bool operator==(const AnimId&) const = default;
};

// How a rolling number runs to a new value. The default is a mechanical counter:
// one unit at a time, each moving for most of its step and then stopping hard.
struct Roll
{
	float stepMs = 40;      // per unit
	float baseMs = 0;       // added once, for eased rolls
	float maxMs = 1600;     // a long way never takes longer: bigger steps, or a faster ease
	Ease ease = Ease::Linear;
	bool stepped = true;    // false: one ease over the whole way
};

class Anim
{
public:
	// durations are multiplied by it; 0 lands everything at once (reduced motion)
	float motionScale = 1.0f;
	// frame interval while anything moves (30 fps)
	int frameMs = 33;

	// Once per frame, before anything reads. Fires the timers and after()s that are due.
	void update(uint64_t nowMs);
	uint64_t now() const { return now_; }

	// --- timers: progress goes 0 to 1 over ms, then done runs once -------------
	// Starting an id that runs restarts it (without its old done). A timer started from
	// another's done begins where that one ended, so chains don't drift with the frames.
	void start(AnimId id, float ms, std::function<void()> done = {});
	// 1 when it has finished, was stopped or never started
	float progress(AnimId id) const;
	bool running(AnimId id) const;
	void stop(AnimId id);
	// runs fn once, ms from now (not scaled by motionScale: it's a clock, not a motion)
	void after(float ms, std::function<void()> fn);

	// --- tweens: a value that eases to target whenever target changes ---------
	// It starts from where it is, so a change mid-way doesn't jump. The first call
	// starts on the target.
	float tween(AnimId id, float target, float ms, Ease e = Ease::Out);
	// jumps there; the next tween to a new target starts from it
	void set(AnimId id, float value);

	// --- springs: a value pulled to target, overshooting when lightly damped ----
	// 26 is about critical damping for a stiffness of 170 (no overshoot). The first
	// call starts at rest on the target.
	float spring(AnimId id, float target, float stiffness = 170.0f, float damping = 26.0f);

	// --- rolling numbers: a counter running to target -----------------------------
	// The first call starts on the target. ticks (optional) gets how many whole units
	// it passed since the last call, for a tick sound per unit.
	float roll(AnimId id, float target, const Roll& how = {}, int* ticks = nullptr);

	// --- the redraw rule ---------------------------------------------------------
	bool moving() const;
	// ms until a frame is needed (0 = now), -1 for none; now is the clock update() gets
	int waitTimeoutMs(uint64_t nowMs) const;

private:
	struct IdHash { size_t operator()(const AnimId& id) const { return id.hash; } };
	template <class T> using Map = std::unordered_map<AnimId, T, IdHash>;

	struct Timer { uint64_t start = 0; float ms = 0; std::function<void()> done; };
	struct Later { uint64_t at = 0; std::function<void()> fn; };
	struct Tween { float from = 0, to = 0; uint64_t start = 0; float ms = 0; Ease ease = Ease::Out; };
	struct Spring { float x = 0, v = 0, target = 0, k = 170, c = 26; bool rest = true; };
	struct Rolling
	{
		float from = 0, to = 0;
		uint64_t start = 0;
		Roll how;
		float ms = 0;      // the whole run
		float stepMs = 0;  // one step (stepped rolls)
		float unit = 0;    // what a step adds (stepped rolls)
		long shown = 0;    // the whole number last reported, for ticks
	};

	float tweenValue(const Tween& t) const;
	float rollValue(const Rolling& r) const;

	uint64_t now_ = 0;
	uint64_t updated_ = 0;
	Map<Timer> timers_;
	std::vector<Later> later_;
	Map<Tween> tweens_;
	Map<Spring> springs_;
	Map<Rolling> rolls_;
};

}
