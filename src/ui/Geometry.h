#pragma once
// -----------------------------------------------------------------------------
// Geometry.h -- Rect and Transform2D, the plain math the 2D layer shares
// -----------------------------------------------------------------------------
// No GL: tests and app code can use them on their own.

#include <glm/glm.hpp>

namespace ui
{

struct Rect
{
	float x = 0, y = 0, w = 0, h = 0;
	bool contains(glm::vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
	Rect inset(float d) const { return { x + d, y + d, w - 2 * d, h - 2 * d }; }
};

// A point p lands at offset + p * scale. One uniform scale, so circles stay circles
// and a shape's edge stays one pixel of antialiasing at any zoom.
struct Transform2D
{
	glm::vec2 offset{ 0.0f, 0.0f };
	float scale = 1.0f;

	glm::vec2 apply(glm::vec2 p) const { return offset + p * scale; }
	glm::vec2 inverse(glm::vec2 p) const { return (p - offset) / scale; }
	Rect apply(const Rect& r) const { return { offset.x + r.x * scale, offset.y + r.y * scale, r.w * scale, r.h * scale }; }
	// this one applied after `inner`: (a * b).apply(p) == a.apply(b.apply(p))
	Transform2D operator*(const Transform2D& inner) const { return { apply(inner.offset), scale * inner.scale }; }
};

}
