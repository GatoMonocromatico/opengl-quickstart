#pragma once
// -----------------------------------------------------------------------------
// Color.h -- colours from hex codes and from OKLCH
// -----------------------------------------------------------------------------
//   ui::rgb(0x283a35)             -> (0.157, 0.227, 0.208, 1)
//   ui::oklch(0.75f, 0.17f, 140)  -> sRGB at an even perceived lightness, for palettes
//                                    that keep the same contrast on every hue

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ui
{

inline glm::vec4 rgb(uint32_t hex, float alpha = 1.0f)
{
	return { ((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, alpha };
}

// OKLCH (lightness 0 to 1, chroma 0 to about 0.37, hue in degrees) to gamma-encoded
// sRGB. What falls outside sRGB is clamped; inGamut says whether it had to be.
inline glm::vec3 oklch(float L, float C, float hueDeg, bool* inGamut = nullptr)
{
	const float h = glm::radians(hueDeg);
	const float a = C * std::cos(h), b = C * std::sin(h);
	auto cube = [](float v) { return v * v * v; };
	const float l = cube(L + 0.3963377774f * a + 0.2158037573f * b);
	const float m = cube(L - 0.1055613458f * a - 0.0638541728f * b);
	const float s = cube(L - 0.0894841775f * a - 1.2914855480f * b);
	const glm::vec3 lin(
		4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
		-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
		-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
	if (inGamut) *inGamut = glm::all(glm::greaterThan(lin, glm::vec3(-0.002f))) && glm::all(glm::lessThan(lin, glm::vec3(1.002f)));
	glm::vec3 out;
	for (int i = 0; i < 3; i++)
	{
		const float v = std::clamp(lin[i], 0.0f, 1.0f);
		out[i] = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
	}
	return out;
}

}
