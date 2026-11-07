#include "ui/Motion.h"

#include <chrono>
#include <utility>

namespace ui
{

namespace motion
{
	namespace
	{
		std::function<double()> gClock;
		double gFrame = 0;
		bool gWant = false;
		bool gReduced = false;
	}

	double clock()
	{
		if (gClock) return gClock();
		using namespace std::chrono;
		return duration<double>(steady_clock::now().time_since_epoch()).count();
	}
	void setClock(std::function<double()> c) { gClock = std::move(c); }

	double now() { return gFrame; }
	void frame()
	{
		gFrame = clock();
		gWant = false;
	}

	bool wantsFrame() { return gWant; }
	void requestFrame() { gWant = true; }
	void setWantsFrame(bool want) { gWant = want; }

	bool reduced() { return gReduced; }
	void setReduced(bool on) { gReduced = on; }
}

float CubicBezier::operator()(float t) const
{
	if (t <= 0) return 0;
	if (t >= 1) return 1;
	// B(s) = 3(1-s)^2 s P1 + 3(1-s) s^2 P2 + s^3, per axis; find s with x(s) = t
	const auto bx = [&](float s) { const float r = 1 - s; return 3 * r * r * s * x1 + 3 * r * s * s * x2 + s * s * s; };
	const auto dx = [&](float s) { const float r = 1 - s; return 3 * r * r * x1 + 6 * r * s * (x2 - x1) + 3 * s * s * (1 - x2); };
	float s = t;
	for (int i = 0; i < 8; i++) // Newton, usually done in 3-4
	{
		const float e = bx(s) - t;
		if (std::abs(e) < 1e-6f) break;
		const float d = dx(s);
		if (std::abs(d) < 1e-6f) { s = -1; break; }
		s -= e / d;
		if (s < 0 || s > 1) { s = -1; break; }
	}
	if (s < 0) // flat or wandering: bisect, x(s) is monotonic for x1, x2 in [0, 1]
	{
		float lo = 0, hi = 1;
		s = t;
		for (int i = 0; i < 40; i++)
		{
			s = (lo + hi) / 2;
			(bx(s) < t ? lo : hi) = s;
		}
	}
	const float r = 1 - s;
	return 3 * r * r * s * y1 + 3 * r * s * s * y2 + s * s * s;
}

}
