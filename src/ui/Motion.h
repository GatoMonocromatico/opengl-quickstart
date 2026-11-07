#pragma once
// -----------------------------------------------------------------------------
// Motion.h -- easing curves, tweens and springs, and the clock they share
// -----------------------------------------------------------------------------
// Every animation reads the frame clock, motion::now(), which the loop advances once
// at the start of each frame, so everything drawn in one frame sees the same time.
//
//   ui::Tween<float> fade{ 0.0f, 0.22f, ui::ease::fadeIn };
//   ui::Spring<glm::vec2> pos{ { 0, 0 } };
//   onOpen:  fade.set(1); pos.set({ 300, 0 });     // from wherever they are now
//   draw:    painter.roundedRect(pos.value().x, ..., { 1, 1, 1, fade.value() });
//
// Pull-based: value() integrates up to the frame time, and while the value is still
// moving it asks for another frame (motion::wantsFrame()). Nothing registers, so tweens
// and springs copy like plain values, and one that is not drawn costs nothing.
// set() retargets from the current value and velocity, so animations are interruptible.
// Reduced motion (motion::setReduced(true)) makes set() jump straight to the target.
// T is float or a glm vector (vec2/3/4, so colours too). Main thread only.

#include <glm/glm.hpp>

#include <cmath>
#include <functional>
#include <type_traits>

namespace ui
{

namespace motion
{
	// seconds on a monotonic clock; setClock replaces it (tests), nullptr restores it
	double clock();
	void setClock(std::function<double()> clock);

	// the frame time: what every value() is evaluated at. frame() samples clock() into
	// it and clears wantsFrame(); ui::Loop calls it when it hands out a frame.
	double now();
	void frame();

	// true when something moved since frame() and needs another frame
	bool wantsFrame();
	void requestFrame();

	// reduced motion: set() snaps to the target instead of animating
	bool reduced();
	void setReduced(bool on);
}

// CSS-style cubic-bezier(x1, y1, x2, y2) through (0,0) and (1,1)
struct CubicBezier
{
	float x1, y1, x2, y2;
	float operator()(float t) const; // eased progress for linear progress t in [0, 1]
};

namespace ease
{
	inline constexpr CubicBezier linear{ 0, 0, 1, 1 };
	// Sisteme's picom curves, so shell surfaces move the way windows do
	inline constexpr CubicBezier enter{ 0.16f, 1, 0.3f, 1 };   // open, move, resize
	inline constexpr CubicBezier fadeIn{ 0.25f, 1, 0.5f, 1 };  // opacity coming in
	inline constexpr CubicBezier leave{ 0.5f, 0, 0.75f, 0 };   // close, fade out
	inline constexpr CubicBezier standard{ 0.4f, 0, 0.2f, 1 }; // on-screen change
}

namespace detail
{
	template <class T> float magnitude(const T& v)
	{
		if constexpr (std::is_arithmetic_v<T>) return std::abs(static_cast<float>(v));
		else return glm::length(v);
	}
}

// Fixed-duration animation along a curve.
template <class T> class Tween
{
public:
	explicit Tween(T value = T{}, float duration = 0.25f, CubicBezier curve = ease::enter)
		: duration(duration), curve(curve), from_(value), to_(value) {}

	float duration;   // seconds
	CubicBezier curve;

	// animate from the value on screen to target; a repeated target doesn't restart it
	void set(T target)
	{
		if (target == to_) return;
		if (motion::reduced() || duration <= 0) { snap(target); return; }
		from_ = at(motion::now());
		to_ = target;
		start_ = motion::clock();
		running_ = true;
		motion::requestFrame();
	}
	void snap(T v) { from_ = to_ = v; if (running_) motion::requestFrame(); running_ = false; }

	T value()
	{
		if (!running_) return to_;
		const double p = (motion::now() - start_) / duration;
		if (p >= 1) { running_ = false; return to_; }
		motion::requestFrame();
		return lerp(p);
	}
	T target() const { return to_; }
	bool moving() const { return running_; }

private:
	T at(double t) const { return running_ ? lerp((t - start_) / duration) : to_; }
	T lerp(double p) const
	{
		const float k = curve(static_cast<float>(p < 0 ? 0 : p > 1 ? 1 : p));
		return from_ + (to_ - from_) * k;
	}

	T from_, to_;
	double start_ = 0;
	bool running_ = false;
};

// Damped spring, the way SwiftUI parameterizes it: response is the period of the
// undamped oscillation in seconds, damping the fraction of critical damping (1 = no
// bounce, below 1 overshoots). Settles when it is within precision of the target and
// barely moving; precision is in the value's own units (0.001 for 0-1, ~0.1 for pixels).
template <class T> class Spring
{
public:
	explicit Spring(T value = T{}, float response = 0.35f, float damping = 0.86f, float precision = 0.001f)
		: response(response), damping(damping), precision(precision), x_(value), v_(T{}), to_(value) {}

	float response, damping, precision;

	// retarget, keeping position and velocity
	void set(T target)
	{
		if (motion::reduced()) { snap(target); return; }
		if (target == to_ && !running_) return;
		to_ = target;
		if (!running_) { last_ = motion::clock(); running_ = true; }
		motion::requestFrame();
	}
	void snap(T v) { x_ = to_ = v; v_ = T{}; if (running_) motion::requestFrame(); running_ = false; }

	T value()
	{
		if (!running_) return x_;
		const double t = motion::now();
		if (t > last_) { step(t - last_); last_ = t; }
		if (running_) motion::requestFrame();
		return x_;
	}
	T target() const { return to_; }
	T velocity() const { return v_; }
	bool moving() const { return running_; }

private:
	void step(double dt)
	{
		constexpr double kMaxStep = 1.0 / 240;
		if (dt > 2.0) { snap(to_); return; } // a stalled app shouldn't integrate seconds of it
		const int n = static_cast<int>(std::ceil(dt / kMaxStep));
		const float h = static_cast<float>(dt / n);
		const float w = 2 * 3.14159265f / (response > 1e-4f ? response : 1e-4f);
		const float k = w * w, c = 2 * damping * w; // unit mass
		for (int i = 0; i < n; i++)
		{
			const T a = (to_ - x_) * k - v_ * c;
			v_ = v_ + a * h; // semi-implicit Euler: velocity first, then position
			x_ = x_ + v_ * h;
		}
		if (detail::magnitude(to_ - x_) < precision && detail::magnitude(v_) * response < precision)
		{
			x_ = to_;
			v_ = T{};
			running_ = false;
		}
	}

	T x_, v_, to_;
	double last_ = 0;
	bool running_ = false;
};

}
