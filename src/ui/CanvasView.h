#pragma once
// -----------------------------------------------------------------------------
// CanvasView.h -- the camera of a pannable, zoomable view onto a 2D world
// -----------------------------------------------------------------------------
// Where the world sits in a view rect and how big it is drawn. No GL and no input:
// ui::Context::beginCanvas() drives one with the mouse and draws through it, and
// tests or app code can do the math with it alone. `view` is the rect on the
// screen the world is seen through.

#include "ui/Geometry.h"

namespace ui
{

struct CanvasView
{
	glm::vec2 offset{ 0.0f, 0.0f };  // where world (0, 0) is, from the view's top-left, in pixels
	float zoom = 1.0f;
	float minZoom = 0.5f, maxZoom = 2.0f;
	float wheelStep = 1.12f;         // zoom factor per wheel notch
	float dragThreshold = 3.0f;      // pixels a press may move and still be a click

	// world (0, 0) on the screen, on a whole pixel so 1 px lines stay crisp at zoom 1
	glm::vec2 origin(const Rect& view) const;
	Transform2D transform(const Rect& view) const { return { origin(view), zoom }; }
	glm::vec2 toScreen(glm::vec2 world, const Rect& view) const { return transform(view).apply(world); }
	glm::vec2 toWorld(glm::vec2 screen, const Rect& view) const { return transform(view).inverse(screen); }

	// zooms by factor, within the limits, keeping the world point under `screen` where it is
	void zoomAt(glm::vec2 screen, float factor, const Rect& view);
	// the zoom (within the limits) and offset that show all of world, centred, margin px in
	void fit(const Rect& world, const Rect& view, float margin = 0.0f);
	// the offset that puts world in the middle of the view, at this zoom
	glm::vec2 centered(glm::vec2 world, const Rect& view) const;
};

}
