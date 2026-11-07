#include "ui/Anim.h"

#include <algorithm>
#include <cmath>

namespace ui
{

// springs integrate in steps of 4 ms at most, so a 33 ms frame stays stable
static constexpr float kSpringStep = 0.004f;
// a step of a stepped roll moves for this much of its time, then holds
static constexpr float kStepMove = 0.7f;

float ease(Ease e, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	switch (e)
	{
	case Ease::In:
		return t * t * t;
	case Ease::Out:
	{
		const float u = 1.0f - t;
		return 1.0f - u * u * u;
	}
	case Ease::InOut:
	{
		if (t < 0.5f) return 4.0f * t * t * t;
		const float u = 2.0f - 2.0f * t;
		return 1.0f - u * u * u * 0.5f;
	}
	case Ease::Back:
	{
		// peaks about 5% past 1 near t = 0.64, then settles
		const float c = 1.2f, u = t - 1.0f;
		return 1.0f + (c + 1.0f) * u * u * u + c * u * u;
	}
	default:
		return t;
	}
}

// --- ids ---------------------------------------------------------------------

static uint32_t fnv(std::string_view s)
{
	uint32_t h = 2166136261u;
	for (unsigned char c : s) h = (h ^ c) * 16777619u;
	return h;
}

AnimId::AnimId(const char* s) : AnimId(std::string_view(s)) {}
AnimId::AnimId(std::string_view s) : hash(fnv(s)) {}
AnimId::AnimId(std::string_view s, int index) : hash(fnv(s))
{
	// a separator byte, then the index's four bytes
	hash = (hash ^ 0xFFu) * 16777619u;
	for (int k = 0; k < 4; k++) hash = (hash ^ ((static_cast<uint32_t>(index) >> (8 * k)) & 0xFFu)) * 16777619u;
}

// --- the clock -----------------------------------------------------------------

void Anim::update(uint64_t nowMs)
{
	const uint64_t was = now_;
	now_ = std::max(nowMs, now_);

	// a long gap (the loop slept, the window was hidden) is cut short rather than integrated
	float dt = std::min(static_cast<float>(now_ - was), 100.0f) / 1000.0f;
	if (motionScale > 0.0f) dt = std::min(dt / motionScale, 1.0f);
	for (auto& [id, s] : springs_)
	{
		if (s.rest) continue;
		if (motionScale > 0.0f)
			for (float left = dt; left > 0.0f; left -= kSpringStep)
			{
				const float h = std::min(left, kSpringStep);
				s.v += (-s.k * (s.x - s.target) - s.c * s.v) * h;
				s.x += s.v * h;
			}
		if (motionScale <= 0.0f || (std::fabs(s.x - s.target) < 1e-3f && std::fabs(s.v) < 1e-2f))
		{
			s.x = s.target;
			s.v = 0.0f;
			s.rest = true;
		}
	}

	// what's due runs in the order it came due; what those start waits for the next update
	struct Due { uint64_t at; std::function<void()> fn; };
	std::vector<Due> due;
	for (auto it = timers_.begin(); it != timers_.end();)
	{
		Timer& t = it->second;
		if (static_cast<float>(now_ - t.start) >= t.ms)
		{
			due.push_back({ t.start + static_cast<uint64_t>(t.ms), std::move(t.done) });
			it = timers_.erase(it);
		}
		else ++it;
	}
	for (auto it = later_.begin(); it != later_.end();)
	{
		if (it->at <= now_)
		{
			due.push_back({ it->at, std::move(it->fn) });
			it = later_.erase(it);
		}
		else ++it;
	}
	std::stable_sort(due.begin(), due.end(), [](const Due& a, const Due& b) { return a.at < b.at; });
	const uint64_t frame = now_;
	for (Due& d : due)
	{
		if (!d.fn) continue;
		// so what it starts begins where this ended
		now_ = d.at;
		d.fn();
	}
	now_ = frame;
}

// --- timers --------------------------------------------------------------------

void Anim::start(AnimId id, float ms, std::function<void()> done)
{
	timers_[id] = { now_, std::max(0.0f, ms * motionScale), std::move(done) };
}

float Anim::progress(AnimId id) const
{
	auto it = timers_.find(id);
	if (it == timers_.end() || it->second.ms <= 0.0f) return 1.0f;
	return std::min(1.0f, static_cast<float>(now_ - it->second.start) / it->second.ms);
}

bool Anim::running(AnimId id) const
{
	auto it = timers_.find(id);
	return it != timers_.end() && static_cast<float>(now_ - it->second.start) < it->second.ms;
}

void Anim::stop(AnimId id) { timers_.erase(id); }

void Anim::after(float ms, std::function<void()> fn)
{
	later_.push_back({ now_ + static_cast<uint64_t>(std::max(0.0f, ms)), std::move(fn) });
}

// --- tweens --------------------------------------------------------------------

float Anim::tweenValue(const Tween& t) const
{
	if (t.ms <= 0.0f) return t.to;
	const float k = static_cast<float>(now_ - t.start) / t.ms;
	return k >= 1.0f ? t.to : t.from + (t.to - t.from) * ease(t.ease, k);
}

float Anim::tween(AnimId id, float target, float ms, Ease e)
{
	auto [it, fresh] = tweens_.try_emplace(id);
	Tween& t = it->second;
	if (fresh) t = { target, target, now_, 0.0f, e };
	else if (target != t.to) t = { tweenValue(t), target, now_, std::max(0.0f, ms * motionScale), e };
	return tweenValue(t);
}

void Anim::set(AnimId id, float value)
{
	tweens_[id] = { value, value, now_, 0.0f, Ease::Out };
}

// --- springs -------------------------------------------------------------------

float Anim::spring(AnimId id, float target, float stiffness, float damping)
{
	auto [it, fresh] = springs_.try_emplace(id);
	Spring& s = it->second;
	if (fresh) s.x = s.target = target;
	s.k = stiffness;
	s.c = damping;
	if (target != s.target)
	{
		s.target = target;
		s.rest = false;
	}
	return s.x;
}

// --- rolling numbers -------------------------------------------------------------

float Anim::rollValue(const Rolling& r) const
{
	const float t = static_cast<float>(now_ - r.start);
	if (r.ms <= 0.0f || t >= r.ms) return r.to;
	if (!r.how.stepped) return r.from + (r.to - r.from) * ease(r.how.ease, t / r.ms);
	const float s = t / r.stepMs;
	const float i = std::floor(s);
	return r.from + r.unit * (i + ease(r.how.ease, std::min(1.0f, (s - i) / kStepMove)));
}

float Anim::roll(AnimId id, float target, const Roll& how, int* ticks)
{
	auto [it, fresh] = rolls_.try_emplace(id);
	Rolling& r = it->second;
	if (fresh)
	{
		r.from = r.to = target;
		r.start = now_;
		r.shown = std::lround(target);
	}
	else if (target != r.to)
	{
		r.from = rollValue(r);
		r.to = target;
		r.start = now_;
		r.how = how;
		const float dist = std::fabs(r.to - r.from);
		const float scale = std::max(0.0f, motionScale);
		if (how.stepped)
		{
			// never longer than maxMs: a long way takes fewer, bigger steps
			const float units = std::max(1.0f, std::ceil(dist - 1e-4f));
			const float steps = std::min(units, std::max(1.0f, std::floor(how.maxMs / std::max(how.stepMs, 1.0f))));
			r.unit = (r.to - r.from) / steps;
			r.stepMs = how.stepMs * scale;
			r.ms = r.stepMs * steps;
		}
		else r.ms = std::min(how.maxMs, how.baseMs + how.stepMs * dist) * scale;
	}
	const float v = rollValue(r);
	const long whole = std::lround(v);
	if (ticks) *ticks = static_cast<int>(std::labs(whole - r.shown));
	r.shown = whole;
	return v;
}

// --- the redraw rule ---------------------------------------------------------------

bool Anim::moving() const
{
	for (const auto& [id, t] : timers_)
		if (static_cast<float>(now_ - t.start) < t.ms) return true;
	for (const auto& [id, t] : tweens_)
		if (static_cast<float>(now_ - t.start) < t.ms) return true;
	for (const auto& [id, r] : rolls_)
		if (static_cast<float>(now_ - r.start) < r.ms) return true;
	for (const auto& [id, s] : springs_)
		if (!s.rest) return true;
	return false;
}

int Anim::waitTimeoutMs(uint64_t nowMs) const
{
	bool any = false;
	uint64_t next = 0;
	auto wake = [&](uint64_t at) {
		if (!any || at < next) next = at;
		any = true;
	};
	if (moving()) wake(now_ + static_cast<uint64_t>(std::max(frameMs, 1)));
	// a timer's end gets its own frame, so its done runs (and its last state shows) on time
	for (const auto& [id, t] : timers_) wake(t.start + static_cast<uint64_t>(t.ms));
	for (const Later& l : later_) wake(l.at);
	if (!any) return -1;
	return next <= nowMs ? 0 : static_cast<int>(next - nowMs);
}

}
